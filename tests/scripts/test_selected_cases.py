"""Preview the exact execution selection without constructing fixtures."""
import json
import subprocess
import sys


def run(binary, *args, code=0):
    result = subprocess.run([binary, "--no-color", *args],
                            capture_output=True, text=True, timeout=30)
    assert result.returncode == code, result
    return result


def main():
    binary = sys.argv[1]
    all_names = ["selection_tests/" + name for name in
                 ("alpha", "beta", "fatal", "latency", "measured", "skipped")]
    checks = (
        ([], [name for name in all_names if not name.endswith("/fatal")]),
        (["--kind=test"], [all_names[i] for i in (0, 1, 5)]),
        (["--kind=bench"], [all_names[4]]),
        (["--kind=jitter"], [all_names[3]]),
        (["--filter=*/?lpha"], [all_names[0]]),
        (["--run=alpha"], [all_names[0]]),
        (["--filter=*/fatal"], []),
        (["--filter=*/fatal", "--include-death"], [all_names[2]]),
        (["--filter=*/skipped", "--fail-on-empty"], [all_names[5]]),
        (["--kind=test", "--filter=missing/*"], []),
        (["--kind=test", "--report-format=json"], [all_names[i] for i in (0, 1, 5)]),
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
    ):
        result = run(binary, "--list-selected", *args, code=code)
        assert result.stdout == "" and message in result.stderr, result

    # Full-inventory modes retain their existing contract and include death cases.
    result = run(binary, "--list-tests", "--kind=bench", "--filter=missing/*")
    assert result.stdout.splitlines() == all_names, result
    inventory = json.loads(run(binary, "--list-json").stdout)
    assert [case["name"] for case in inventory] == all_names, inventory

    # The same selector really executes the promised case; a matched skip is
    # successful under fail-on-empty without executing its failure body.
    result = run(binary, "--kind=test", "--filter=*/?lpha", "--fail-on-empty")
    assert "alpha executed" in result.stdout and "beta executed" not in result.stdout, result
    result = run(binary, "--kind=test", "--filter=*/skipped", "--fail-on-empty")
    assert "[ SKIP ]" in result.stdout and "[ FAIL ]" not in result.stderr, result
    print("selected-case preview and execution agree")


if __name__ == "__main__":
    main()
