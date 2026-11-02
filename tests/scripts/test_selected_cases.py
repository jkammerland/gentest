"""Preview the exact execution selection without constructing fixtures."""
import json
import subprocess
import tempfile
from pathlib import Path
import sys

from target_command import target_command


def run(binary, *args, code=0):
    result = subprocess.run(target_command(binary, "--no-color", *args),
                            capture_output=True, text=True, timeout=30)
    assert result.returncode == code, result
    return result


def main():
    binary = sys.argv[1]
    all_names = ["selection_tests/" + name for name in
                 ("alpha", "beta", "fatal", "latency", "measured", "skipped", "untagged")]
    checks = (
        ([], [name for name in all_names if not name.endswith("/fatal")]),
        (["--kind=test"], [all_names[i] for i in (0, 1, 5, 6)]),
        (["--kind=bench"], [all_names[4]]),
        (["--kind=jitter"], [all_names[3]]),
        (["--filter=*/?lpha"], [all_names[0]]),
        (["--run=alpha"], [all_names[0]]),
        (["--run=alpha", "--filter=*/beta", "--tag=fast"], [all_names[0]]),
        (["--filter=*/fatal"], []),
        (["--filter=*/fatal", "--include-death"], [all_names[2]]),
        (["--filter=*/skipped", "--fail-on-empty"], [all_names[5]]),
        (["--kind=test", "--filter=missing/*"], []),
        (["--kind=test", "--report-format=json"], [all_names[i] for i in (0, 1, 5, 6)]),
        (["--tag=FaSt"], [all_names[i] for i in (0, 4)]),
        (["--tag=fast", "--tag", "slow", "--tag=FAST"], [all_names[i] for i in (0, 1, 3, 4, 5)]),
        (["--tag=fast", "--exclude-tag=FAST", "--include-death"], []),
        (["--tag=fast", "--tag=slow", "--exclude-tag=slow", "--exclude-tag=death"], [all_names[i] for i in (0, 4)]),
        (["--tag=slow", "--exclude=selection_tests/b*", "--exclude", "*/?atency"], [all_names[5]]),
        (["--tag=fast", "--kind=test", "--filter=*/a*"], [all_names[0]]),
        (["--tag=fast", "--kind=test", "--filter=*/A*"], []),
        (["--run=alpha", "--exclude=*/alpha"], []),
        (["--run=alpha", "--tag=slow"], []),
        (["--tag=death"], []),
        (["--tag=DeAtH", "--include-death"], [all_names[2]]),
        (["--tag=death", "--include-death", "--exclude-tag=death"], []),
        (["--tag=missing"], []),
        (["--exclude=*"], []),
        (["--exclude-tag=fast"], [all_names[i] for i in (1, 3, 5, 6)]),
        (["--run=alpha", "--exclude=*/Alpha"], [all_names[0]]),
    )
    for args, expected in checks:
        result = run(binary, "--list-selected", *args)
        assert result.stdout.splitlines() == expected, result
        assert result.stderr == "", result

    # Preview preserves execution errors, including exact-name miss status.
    for args, code, message in (
        (["--run=missing"], 3, "Case not found"),
        (["--run=alpha", "--kind=bench"], 1, "does not match"),
        (["--kind=bench", "--filter=missing/*"], 1, "matched 0"),
        (["--kind=test", "--filter=missing/*", "--fail-on-empty"], 1, "No cases selected"),
        (["--filter=*/fatal", "--fail-on-empty"], 1, "death tests excluded"),
        (["--list-json"], 1, "another listing mode"),
        (["--list-tags"], 1, "another listing mode"),
        (["--tag=missing", "--fail-on-empty"], 1, "No cases selected"),
        (["--exclude=*", "--fail-on-empty"], 1, "No cases selected"),
        (["--tag=death", "--fail-on-empty"], 1, "death tests excluded"),
    ):
        result = run(binary, "--list-selected", *args, code=code)
        assert result.stdout == "" and message in result.stderr, result

    # Empty death previews stay read-only even when execution report paths exist.
    with tempfile.TemporaryDirectory() as tmp:
        report = Path(tmp) / "preview.xml"
        result = run(binary, "--list-selected", "--tag=death", f"--junit={report}")
        assert result.stdout == "" and result.stderr == "", result
        assert not report.exists(), report

    # Empty/missing values are errors in both supported spellings. No fixture
    # should be constructed while parsing an invalid selector.
    for option in ("--tag", "--exclude-tag", "--exclude"):
        for args in ((option,), (option + "=",), (option, "")):
            result = run(binary, *args, code=1)
            assert result.stdout == "" and "requires a" in result.stderr, result

    # Available tags include skipped/death metadata and ignore selection flags.
    result = run(binary, "--list-tags", "--tag=missing", "--kind=bench")
    assert result.stdout.splitlines() == ["death", "fast", "slow"], result
    assert result.stderr == "", result
    result = run(binary, "--list-tags", "--list-json", code=1)
    assert "another listing mode" in result.stderr, result

    # Full-inventory modes retain their existing contract and include death cases.
    result = run(binary, "--list-tests", "--kind=bench", "--filter=missing/*")
    assert result.stdout.splitlines() == all_names, result
    inventory = json.loads(run(binary, "--list-json").stdout)
    assert [case["name"] for case in inventory] == all_names, inventory

    # Execution must reject an empty selection before constructing Probe too.
    result = run(binary, "--kind=test", "--filter=missing/*", "--fail-on-empty", code=1)
    assert result.stdout == "" and "No cases selected" in result.stderr, result

    # The same selector really executes the promised case; a matched skip is
    # successful under fail-on-empty without executing its failure body.
    result = run(binary, "--kind=test", "--filter=*/?lpha", "--fail-on-empty")
    assert "alpha executed" in result.stdout and "beta executed" not in result.stdout, result
    assert "selection fixture setup" in result.stdout, result
    result = run(binary, "--kind=test", "--filter=*/skipped", "--fail-on-empty")
    assert "[ SKIP ]" in result.stdout and "[ FAIL ]" not in result.stderr, result
    result = run(binary, "--kind=test", "--tag=fast", "--tag", "slow", "--exclude-tag=slow")
    assert "alpha executed" in result.stdout and "beta executed" not in result.stdout, result
    result = run(binary, "--tag=missing")
    assert result.stdout == "Executed 0 case(s).\n" and result.stderr == "", result
    result = run(binary, "--tag=fast", "--exclude=*")
    assert result.stdout == "Executed 0 case(s).\n" and result.stderr == "", result
    print("selected-case preview and execution agree")


if __name__ == "__main__":
    main()
