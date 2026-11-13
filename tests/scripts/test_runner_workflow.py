"""Exercise runner output, exact failure commands, and printed shuffle replays."""

import csv
import io
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

from target_command import target_command


ENV = dict(os.environ, NO_COLOR="1", GITHUB_ACTIONS="", GENTEST_NO_ASYNC_LIVE="1")


def run(binary, *args, expected=0):
    result = subprocess.run(target_command(binary, *args), capture_output=True, text=True, env=ENV, timeout=30)
    assert result.returncode == expected, (result.args, result.returncode, result.stdout, result.stderr)
    return result


def printed_command(command, expected):
    if os.name == "nt":
        invocation = ["powershell", "-NoProfile", "-Command", command + "; exit $LASTEXITCODE"]
    else:
        # Shell execution checks quoting too, while preserving cross-test emulators.
        prefix = target_command("__target__")[:-1]
        invocation = " ".join(shlex.quote(arg) for arg in prefix)
        invocation = (invocation + " " + command).lstrip()
    result = subprocess.run(invocation, shell=os.name != "nt", capture_output=True, text=True, env=ENV, timeout=30)
    assert result.returncode == expected, (command, result.returncode, result.stdout, result.stderr)
    return result


def check_output(binary):
    result = run(binary, "--filter=workflow/sync/*", "--output=failures-only", "--no-color", expected=1)
    assert "[ PASS ]" not in result.stdout and "[ SKIP ]" not in result.stdout and "[ XFAIL ]" not in result.stdout, result
    assert "passing body marker" not in result.stdout + result.stderr, result
    assert "expected failure marker" not in result.stdout + result.stderr, result
    assert result.stderr.count("failing body marker") == 1, result
    assert result.stderr.count("failing worker marker") == 1, result
    assert result.stderr.count("unexpected pass marker") == 1, result
    assert "workflow deliberate failure" in result.stderr, result
    assert "[ FAIL ]" in result.stderr and "[ XPASS ]" in result.stderr, result
    assert "Summary: passed 1/5; failed 2; skipped 2; blocked 0; xfail 1; xpass 1." in result.stdout, result

    compact = run(binary, "--filter=workflow/sync/*", "--output=compact", "--no-color", expected=1)
    assert compact.stdout.startswith("F.sxF\nSummary:"), compact
    assert "passing body marker" not in compact.stdout + compact.stderr, compact
    assert compact.stderr.count("failing worker marker") == 1, compact

    passing = run(binary, "--run=workflow/sync/pass", "--log-output=failures", "--no-color")
    assert "[ PASS ]" in passing.stdout and "passing body marker" not in passing.stdout + passing.stderr, passing
    explicit_all = run(binary, "--run=workflow/sync/pass", "--output=failures-only", "--log-output=all", "--no-color")
    assert "passing body marker" in explicit_all.stdout and "[ PASS ]" not in explicit_all.stdout, explicit_all

    asynchronous = run(binary, "--filter=workflow/async/*", "--output=failures-only", "--repeat=2", expected=1)
    assert "passing async" not in asynchronous.stdout + asynchronous.stderr, asynchronous
    assert asynchronous.stderr.count("failing async before yield") == 2, asynchronous
    assert asynchronous.stderr.count("failing async after yield") == 2, asynchronous
    assert "Summary: passed 2/4; failed 2;" in asynchronous.stdout, asynchronous
    assert "\x1b" not in asynchronous.stdout + asynchronous.stderr, asynchronous
    assert asynchronous.stderr.count("Rerun:") == 1, asynchronous

    restoration = run(binary, "--probe=restoration")
    assert restoration.stdout.count("passing body marker") == 1, restoration
    assert "passing async" not in restoration.stdout + restoration.stderr, restoration
    assert restoration.stderr.count("failing async before yield") == 2, restoration

    blocked = run(binary, "--probe=blocked", expected=1)
    assert "[ BLOCKED ] blocked/case" in blocked.stderr and "deliberate unavailable fixture" in blocked.stderr, blocked
    assert blocked.stderr.count("fixture allocation marker") == 1, blocked
    assert "passing body marker" not in blocked.stdout + blocked.stderr, blocked
    assert "blocked 1;" in blocked.stdout, blocked


def check_reports(binary, directory):
    for format_name in ("csv", "json"):
        result = run(binary, "--run=workflow/measured/failure", "--report-format=" + format_name,
                     "--output=failures-only", expected=1)
        if format_name == "json":
            report = json.loads(result.stdout)
            assert report["issues"], report
        else:
            rows = list(csv.reader(io.StringIO(result.stdout)))
            assert rows[0] == ["report", "table", "row", "field", "type", "value"], rows
            assert len(rows) > 1 and all(len(row) == 6 for row in rows), rows
        for marker in ("measured setup marker", "measured body marker", "measured teardown marker"):
            assert marker not in result.stdout and result.stderr.count(marker) == 1, result

    junit = directory / "captured.xml"
    result = run(binary, "--filter=workflow/async/*", "--output=failures-only", "--repeat=2",
                 "--junit=" + str(junit), expected=1)
    root = ET.parse(junit).getroot()
    cases = root.findall(".//testcase")
    assert len(cases) == 4 and sum(case.find("failure") is not None for case in cases) == 2, result
    assert "passing async" not in result.stdout + result.stderr, result

    # JUnit exports outcomes and failure text, independent of console presentation.
    ordinary_junit = directory / "ordinary.xml"
    run(binary, "--filter=workflow/async/*", "--repeat=2", "--junit=" + str(ordinary_junit), expected=1)

    def signature(report):
        return sorted((case.attrib["name"], case.attrib["classname"],
                       tuple(failure.text for failure in case.findall("failure")))
                      for case in report.findall(".//testcase"))

    assert signature(root) == signature(ET.parse(ordinary_junit).getroot()), result


def check_reruns(binary):
    inventory = json.loads(run(binary, "--list-json").stdout)
    failures = [item["name"] for item in inventory if "/fail[" in item["name"]]
    assert len(failures) == 2, failures
    for name in failures:
        failed = run(binary, "--run=" + name, "--no-color", expected=1)
        commands = re.findall(r"^Rerun: (.+)$", failed.stderr, re.MULTILINE)
        assert len(commands) == 1, failed
        if os.name != "nt":
            parsed = shlex.split(commands[0])
            assert parsed[1:] == ["--run=" + name, "--kind=test", "--no-color"], parsed
        rerun = printed_command(commands[0], expected=1)
        assert "Summary: passed 0/1; failed 1;" in rerun.stdout, rerun
        assert name in rerun.stderr, rerun

    ambiguous = run(binary, "--filter=duplicates/*", "--no-color", expected=1)
    assert "Rerun:" not in ambiguous.stdout + ambiguous.stderr, ambiguous


def case_order(output):
    return re.findall(r"^\[ PASS \] (shuffle/\S+) \(", output, re.MULTILINE)


def check_shuffle(binary):
    inventory = json.loads(run(binary, "--list-json").stdout)
    names = {item["name"] for item in inventory if item["name"].startswith("shuffle/")}
    assert names, inventory
    for base in (0, 2**64 - 1):
        result = run(binary, "--filter=shuffle/*", "--kind=test", "--shuffle", "--seed=" + str(base), "--repeat=3", "--no-color")
        seeds = [int(seed) for seed in re.findall(r"^Shuffle seed: (\d+) ", result.stdout, re.MULTILINE)]
        assert seeds == [(base + 0x9E3779B97F4A7C15 * index) % 2**64 for index in range(3)], result
        commands = re.findall(r"^Replay iteration: (.+)$", result.stdout, re.MULTILINE)
        assert len(commands) == 3, result
        order = case_order(result.stdout)
        assert len(order) == 3 * len(names), result
        iterations = [order[index:index + len(names)] for index in range(0, len(order), len(names))]
        assert len({tuple(iteration) for iteration in iterations}) == 3, iterations
        for iteration, command in zip(iterations, commands, strict=True):
            assert set(iteration) == names and len(iteration) == len(names), iteration
            groups = [name.split("/")[1] for name in iteration]
            assert groups == sorted(groups, key=("free", "suite_a", "suite_b", "global_a", "global_b").index), groups
            replay = printed_command(command, expected=0)
            assert case_order(replay.stdout) == iteration, (command, iteration, replay.stdout)
            assert "iteration 1/1" in replay.stdout, replay

    # An option-shaped case name passed as a separate value must survive replay.
    option_name = run(binary, "--run", "--seed", "--shuffle", "--seed=7", "--repeat", "2", "--no-color")
    commands = re.findall(r"^Replay iteration: (.+)$", option_name.stdout, re.MULTILINE)
    assert len(commands) == 2, option_name
    for command in commands:
        replay = printed_command(command, expected=0)
        assert "[ PASS ] --seed" in replay.stdout and "iteration 1/1" in replay.stdout, replay


def check_invalid_options(binary):
    for arguments, message in (
        (("--output=unknown",), "--output must be one of"),
        (("--output=",), "--output requires a non-empty value"),
        (("--output",), "--output requires a value"),
        (("--output=full", "--output=compact"), "duplicate --output"),
        (("--log-output=unknown",), "--log-output must be one of"),
        (("--log-output=",), "--log-output requires a non-empty value"),
        (("--log-output",), "--log-output requires a value"),
        (("--log-output=all", "--log-output=failures"), "duplicate --log-output"),
        # Single-valued options reject repetition instead of choosing a winner.
        (("--seed=1", "--seed=2"), "duplicate --seed"),
        (("--seed", "1", "--seed=1"), "duplicate --seed"),
        (("--repeat=1", "--repeat=2"), "duplicate --repeat"),
        (("--kind=test", "--kind=bench"), "duplicate --kind"),
        (("--bench-epochs=2", "--bench-epochs=3"), "duplicate --bench-epochs"),
        (("--bench-warmup=0", "--bench-warmup=1"), "duplicate --bench-warmup"),
        (("--bench-min-epoch-time-s=0", "--bench-min-epoch-time-s=1"), "duplicate --bench-min-epoch-time-s"),
        (("--bench-min-total-time-s=0", "--bench-min-total-time-s=1"), "duplicate --bench-min-total-time-s"),
        (("--bench-max-total-time-s=0", "--bench-max-total-time-s=1"), "duplicate --bench-max-total-time-s"),
        (("--jitter-bins=5", "--jitter-bins=6"), "duplicate --jitter-bins"),
        # Out-of-range counts are rejected, never adjusted silently.
        (("--repeat=0",), "--repeat must be between 1 and 1000000"),
        (("--repeat=1000001",), "--repeat must be between 1 and 1000000"),
        (("--bench-epochs=0",), "--bench-epochs must be at least 1"),
        # A huge histogram must fail at parse time, before anything is measured.
        (("--jitter-bins=0",), "--jitter-bins must be between 1 and 10000"),
        (("--jitter-bins=10001",), "--jitter-bins must be between 1 and 10000"),
        (("--jitter-bins=2147483647",), "--jitter-bins must be between 1 and 10000"),
    ):
        result = run(binary, *arguments, expected=1)
        assert message in result.stderr and not result.stdout, result


def main():
    binary = sys.argv[1]
    check_output(binary)
    check_invalid_options(binary)
    run(binary, "--probe=debugger")
    check_shuffle(binary)
    with tempfile.TemporaryDirectory(prefix="gentest runner ' quoted ") as temporary:
        directory = Path(temporary)
        check_reports(binary, directory)
        # The executable path itself needs shell quoting as well as case names.
        copied = directory / Path(binary).name
        shutil.copy2(binary, copied)
        check_reruns(copied)
    print("runner workflow contract passed")


if __name__ == "__main__":
    main()
