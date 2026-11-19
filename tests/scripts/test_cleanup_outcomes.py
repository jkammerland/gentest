"""Observe cleanup through the runner interface and fixture side effects."""
from pathlib import Path
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

from target_command import target_command


def run(folder, *args):
    report = folder / "result.xml"
    report.unlink(missing_ok=True)
    command = target_command(sys.argv[1], *args, "--no-color", f"--junit={report}")
    result = subprocess.run(command, capture_output=True, text=True, timeout=20)
    assert result.returncode == 1, (command, result.stdout, result.stderr)
    return ET.parse(report).getroot(), result.stderr.splitlines()


def main(folder):
    report, _ = run(folder, "--run=cleanup_outcomes/blocked")
    error = report.find("testcase/error")
    assert report.get("errors") == "1" and error is not None, ET.tostring(report)
    assert "close dependency unavailable" in ET.tostring(error, encoding="unicode")

    report, _ = run(folder, "--run=cleanup_outcomes/assertion")
    failure = report.findtext("testcase/failure", "")
    assert report.get("failures") == "1", ET.tostring(report)
    assert "cleanup invariant failed" in failure and "missing prerequisite" in failure, failure

    report, trace = run(folder, "--filter=cleanup_outcomes/cancel/*", "--fail-fast")
    assert "peer failure" in ET.tostring(report, encoding="unicode")
    assert [line for line in trace if line.startswith("hook:")] == [
        "hook: last started", "hook: timer started", "hook: yielding", "hook: released"
    ], trace


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as directory:
        main(Path(directory))
