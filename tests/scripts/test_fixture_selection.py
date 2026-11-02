"""Validate selective fixture lifecycles and mixed-group skip reporting."""
import subprocess
import sys

from target_command import target_command


def main():
    result = subprocess.run(target_command(sys.argv[1]), capture_output=True,
                            text=True, timeout=30)
    assert result.returncode == 0, result
    assert "fixture selection lifecycle contract passed" in result.stdout, result

    # The C++ driver checks activation counters, dependencies, repeats, timing
    # phases, and destruction. Independently check mixed-group case outcomes.
    for mode in ("sync", "async"):
        skipped = f"manual/{mode}_static_skip"
        blocked = f"manual/{mode}_unavailable"
        assert result.stdout.count(f"[ SKIP ] {skipped} ") == 2, result
        assert f"[ BLOCKED ] {skipped}" not in result.stdout, result
        assert result.stdout.count(f"[ BLOCKED ] {blocked} ") == 2, result


if __name__ == "__main__":
    main()
