#!/usr/bin/env python3
"""
Verify that gentest_codegen produces identical outputs in TU wrapper mode when
running serial vs parallel.

This script:
  1) Extracts the exact gentest_codegen command for a target from build.ninja.
  2) Runs it once with GENTEST_CODEGEN_JOBS=<serial_jobs> (default 1).
  3) Captures hashes of generated outputs (tu_*.gentest.h + mock outputs).
  4) Runs it N times with GENTEST_CODEGEN_JOBS=<parallel_jobs> (default 0=auto).
  5) Fails if any parallel run differs from the serial baseline.

Usage:
  python3 scripts/verify_codegen_parallel.py --build-dir build/debug-system \\
    --target gentest_codegen_parallel_bench_obj --repeats 5 --parallel-jobs 0
"""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import subprocess
import sys
from pathlib import Path


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def parse_codegen_commands(build_ninja: Path) -> dict[str, str]:
    lines = build_ninja.read_text(encoding="utf-8", errors="replace").splitlines()

    mapping: dict[str, str] = {}
    current_cmd: str | None = None
    cmd_re = re.compile(r"^\s*COMMAND\s*=\s*(.+)$")
    desc_re = re.compile(r"^\s*DESC\s*=\s*Running gentest_codegen for target\s+([\w_]+)\s*$")

    for line in lines:
        if line.startswith("build "):
            current_cmd = None
            continue
        m = cmd_re.match(line)
        if m:
            current_cmd = m.group(1)
            continue
        m = desc_re.match(line)
        if m and current_cmd:
            mapping[m.group(1)] = current_cmd

    return mapping


def extract_arg(command: str, name: str) -> str | None:
    # Best-effort extraction for `--arg value` with non-space paths (our build dirs).
    m = re.search(rf"(?:^|\s){re.escape(name)}\s+([^\s]+)", command)
    if not m:
        return None
    return m.group(1)


def collect_outputs(command: str) -> list[Path]:
    tu_out_dir = extract_arg(command, "--tu-out-dir")
    if not tu_out_dir:
        raise RuntimeError("Could not find --tu-out-dir in codegen command (TU wrapper mode required).")

    out_dir = Path(tu_out_dir)
    outputs: list[Path] = []

    outputs.extend(sorted(out_dir.glob("*.gentest.h")))

    mock_registry = extract_arg(command, "--mock-registry")
    if mock_registry:
        p = Path(mock_registry)
        if p.exists():
            outputs.append(p)

    mock_impl = extract_arg(command, "--mock-impl")
    if mock_impl:
        p = Path(mock_impl)
        if p.exists():
            outputs.append(p)

    return outputs


def hash_outputs(paths: list[Path]) -> dict[str, str]:
    return {str(p): sha256_file(p) for p in paths}


def run_codegen(command: str, jobs: int) -> None:
    env = os.environ.copy()
    env["GENTEST_CODEGEN_JOBS"] = str(jobs)
    proc = subprocess.run(command, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env, text=False)
    if proc.returncode != 0:
        sys.stderr.write("[verify] gentest_codegen invocation failed\n")
        if proc.stdout:
            sys.stderr.write(proc.stdout.decode(errors="replace"))
        if proc.stderr:
            sys.stderr.write(proc.stderr.decode(errors="replace"))
        raise subprocess.CalledProcessError(proc.returncode, command, output=proc.stdout, stderr=proc.stderr)


def diff_hashes(a: dict[str, str], b: dict[str, str]) -> list[str]:
    diffs: list[str] = []
    a_keys = set(a.keys())
    b_keys = set(b.keys())

    for k in sorted(a_keys - b_keys):
        diffs.append(f"missing in parallel: {k}")
    for k in sorted(b_keys - a_keys):
        diffs.append(f"extra in parallel:   {k}")
    for k in sorted(a_keys & b_keys):
        if a[k] != b[k]:
            diffs.append(f"content differs:     {k}")
    return diffs


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--build-dir", required=True, help="CMake build dir containing build.ninja")
    ap.add_argument("--target", default="gentest_codegen_parallel_bench_obj", help="Target name used in build.ninja DESC")
    ap.add_argument("--serial-jobs", type=int, default=1)
    ap.add_argument("--parallel-jobs", type=int, default=0, help="0 means auto (hardware_concurrency)")
    ap.add_argument("--repeats", type=int, default=3, help="Number of parallel runs to compare against serial baseline")
    args = ap.parse_args()

    build_dir = Path(args.build_dir)
    build_ninja = build_dir / "build.ninja"
    if not build_ninja.exists():
        print(f"error: build.ninja not found: {build_ninja}", file=sys.stderr)
        return 2

    commands = parse_codegen_commands(build_ninja)
    command = commands.get(args.target)
    if not command:
        print(f"error: no gentest_codegen command found for target '{args.target}'", file=sys.stderr)
        return 2

    print(f"[verify] Target: {args.target}")
    print(f"[verify] Serial jobs:   {args.serial_jobs}")
    print(f"[verify] Parallel jobs: {args.parallel_jobs}")
    print(f"[verify] Repeats:       {args.repeats}")

    print("[verify] Running serial baseline ...")
    run_codegen(command, args.serial_jobs)
    outputs = collect_outputs(command)
    if not outputs:
        print("error: no output files found to compare (did codegen run?)", file=sys.stderr)
        return 2
    baseline = hash_outputs(outputs)

    for i in range(args.repeats):
        print(f"[verify] Running parallel (iteration {i + 1}/{args.repeats}) ...")
        run_codegen(command, args.parallel_jobs)
        current = hash_outputs(outputs)
        diffs = diff_hashes(baseline, current)
        if diffs:
            print("[verify] FAIL: parallel output differs from serial baseline:", file=sys.stderr)
            for d in diffs:
                print(f"  {d}", file=sys.stderr)
            return 1

    print("[verify] PASS: parallel output matches serial baseline.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
