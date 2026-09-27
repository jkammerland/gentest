#!/usr/bin/env python3
"""Run strict expected-failure checks for standalone upstream Clang leak probes."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


KNOWN_SYMBOLS = {
    "dependent": "clang::DependentDiagnostic::Create(",
    "deduction": "clang::MakeDeductionFailureInfo(",
}
LEAK_EXIT = 86
LEAK_HEADER = re.compile(r"^(?:Direct|Indirect) leak of (\d+) byte\(s\) in (\d+) object\(s\) allocated from:$", re.M)
SUMMARY = re.compile(r"^SUMMARY: AddressSanitizer: (\d+) byte\(s\) leaked in (\d+) allocation\(s\)\.$", re.M)


def classify(kind: str, returncode: int | None, output: str, *, control: bool = False) -> tuple[str, str]:
    role = "control" if control else "candidate"
    if output.count(f"PROBE_COMPLETED {kind} {role}") != 1:
        return "FAIL", "Probe did not complete the intended ownership path"
    if returncode == 0:
        if re.search(r"Sanitizer|runtime error:|DEADLYSIGNAL|ERROR:|SUMMARY:", output):
            return "FAIL", "Unexpected diagnostic on a successful process"
        return ("PASS", "Control completed cleanly") if control else ("XPASS", "Known leak no longer reproduced; review expectation")
    if control:
        return "FAIL", "Control must complete without leaks or other errors"
    if returncode != LEAK_EXIT:
        return "FAIL", "Unexpected exit code, signal, or timeout"
    if len(re.findall(r"ERROR: LeakSanitizer: detected memory leaks", output)) != 1:
        return "FAIL", "Expected exactly one LeakSanitizer report"
    if re.search(r"ERROR: (?!LeakSanitizer:)|runtime error:|DEADLYSIGNAL|WARNING:.*(?:symboliz|Sanitizer)", output):
        return "FAIL", "Additional sanitizer or symbolization diagnostic"
    headers = list(LEAK_HEADER.finditer(output))
    summaries = list(SUMMARY.finditer(output))
    if not headers or len(summaries) != 1:
        return "FAIL", "Missing or malformed allocation report"
    summary = summaries[0]
    if summary.start() < headers[-1].end():
        return "FAIL", "Malformed allocation report ordering"
    sizes = objects = 0
    for index, header in enumerate(headers):
        end = headers[index + 1].start() if index + 1 < len(headers) else summary.start()
        stack = output[header.end():end]
        if KNOWN_SYMBOLS[kind] not in stack or not re.search(r"^\s+#\d+ ", stack, re.M):
            return "FAIL", "An allocation lacks the recognized symbolized Clang stack"
        sizes += int(header[1])
        objects += int(header[2])
    if sizes != int(summary[1]) or objects != int(summary[2]):
        return "FAIL", "Allocation totals do not match the complete report"
    return "XFAIL", "Only the reproduced upstream ownership leak was reported"


def run_probe(binary: Path, kind: str, directory: Path, timeout: float = 60) -> dict:
    directory.mkdir(parents=True, exist_ok=False)
    env = os.environ.copy()
    # Never inherit a local suppression or an option that disables leak detection.
    env["ASAN_OPTIONS"] = "detect_leaks=1:fast_unwind_on_malloc=0:symbolize=1"
    env["LSAN_OPTIONS"] = f"exitcode={LEAK_EXIT}"
    attempts = []
    for control in (True, False):
        role = "control" if control else "candidate"
        command = [str(binary), kind] + (["control"] if control else [])
        try:
            result = subprocess.run(command, capture_output=True, text=True, errors="replace", env=env, timeout=timeout)
            code, output = result.returncode, result.stdout + result.stderr
        except subprocess.TimeoutExpired as error:
            code, output = None, f"Probe timed out after {timeout}s\n"
            for stream in (error.stdout, error.stderr):
                output += stream.decode(errors="replace") if isinstance(stream, bytes) else stream or ""
        except OSError as error:
            code, output = None, f"Cannot execute probe: {error}\n"
        (directory / f"{role}.log").write_text(output, encoding="utf-8")
        status, reason = classify(kind, code, output, control=control)
        attempts.append({"role": role, "status": status, "reason": reason, "exit_code": code})
        if control and status != "PASS":
            break
    final = attempts[-1]["status"]
    report = {"probe": kind, "status": final, "attempts": attempts,
              "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest() if binary.is_file() else None}
    (directory / "result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--probe", choices=KNOWN_SYMBOLS, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    report = run_probe(args.binary.resolve(), args.probe, args.output)
    summary = f"{report['probe']}: {report['status']} — {report['attempts'][-1]['reason']}\n"
    print(summary, end="")
    (args.output / "summary.md").write_text(summary, encoding="utf-8")
    return 0 if report["status"] == "XFAIL" else 1


if __name__ == "__main__":
    raise SystemExit(main())
