"""Check cleanup and stop-callback lifetimes through the runner and its JUnit report."""
from pathlib import Path
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

from target_command import target_command


def run(root, selector, status, *, tests=1, failures=0, errors=0):
    report = root / "result.xml"
    report.unlink(missing_ok=True)
    args = target_command(sys.argv[1], selector, "--skip-return-code=77", "--no-color", f"--junit={report}")
    proc = subprocess.run(args, capture_output=True, text=True, timeout=30)
    output = proc.stdout + proc.stderr
    assert proc.returncode == status, (args, proc.returncode, output)
    suite = ET.parse(report).getroot()
    for key, expected in (("tests", tests), ("failures", failures), ("errors", errors), ("skipped", 0)):
        assert int(suite.attrib[key]) == expected, ET.tostring(suite)
    return suite, output


def case(suite, name):
    item = suite.find(f"testcase[@name='fixture_lifetime/{name}']")
    assert item is not None and item.find("skipped") is None, ET.tostring(suite)
    return item


def check_cleanup(item, reason, issue):
    failures = item.findall("failure")
    assert len(failures) == 1 and issue in failures[0].text and reason in failures[0].text, ET.tostring(item)
    count = item.find("properties/property[@name='gentest.property.release_count']")
    assert count is not None and count.attrib["value"] == "2", ET.tostring(item)


def main(root):
    suite, _ = run(root, "--filter=fixture_lifetime/sync/*", 1, tests=2, failures=1)
    check_cleanup(case(suite, "sync/01_skip"), "sync prerequisite unavailable", "second fixture failed to close")
    assert case(suite, "sync/02_following").find("failure") is None, ET.tostring(suite)

    suite, _ = run(root, "--run=fixture_lifetime/async/01_skip", 1, failures=1)
    check_cleanup(case(suite, "async/01_skip"), "async prerequisite unavailable", "async fixture failed to close")

    suite, _ = run(root, "--run=fixture_lifetime/setup_skip", 1, failures=1)
    check_cleanup(case(suite, "setup_skip"), "setup prerequisite unavailable", "second fixture failed to close")

    suite, _ = run(root, "--run=fixture_lifetime/teardown_skip", 1, failures=1)
    check_cleanup(case(suite, "teardown_skip"), "cleanup prerequisite unavailable", "second fixture failed to close")

    suite, _ = run(root, "--run=fixture_lifetime/body_and_teardown_skip", 1, failures=1)
    check_cleanup(case(suite, "body_and_teardown_skip"), "original body skip", "second fixture failed to close")

    suite, _ = run(root, "--run=fixture_lifetime/body_throws", 1, failures=1)
    item = case(suite, "body_throws")
    assert "body exception before cleanup" in item.findtext("failure", ""), ET.tostring(item)
    count = item.find("properties/property[@name='gentest.property.release_count']")
    assert count is not None and count.attrib["value"] == "2", ET.tostring(item)

    # The measured wrapper must also destroy its persistent fixture handles while reporting is active.
    for name in ("measured_cleanup_bench", "measured_cleanup_jitter"):
        suite, _ = run(root, f"--run=fixture_lifetime/{name}", 1, failures=1)
        item = case(suite, name)
        check_cleanup(item, "measurement prerequisite unavailable", "second fixture failed to close")
        destroyed = item.find("properties/property[@name='gentest.property.destroyed_during_teardown']")
        assert destroyed is not None and destroyed.attrib["value"] == "true", ET.tostring(item)

    suite, _ = run(root, "--run=fixture_lifetime/shared_missing", 1, errors=1)
    assert case(suite, "shared_missing").find("failure") is None, ET.tostring(suite)
    assert "expected 1 call(s) to ::mocking::Calculator::reset but observed 0" in suite.findtext("system-err", ""), ET.tostring(suite)

    suite, output = run(root, "--run=fixture_lifetime/worker_stop", 0)
    assert case(suite, "worker_stop").find("failure") is None, ET.tostring(suite)
    assert "stop callback joined worker" in output and "nested callback lease" in output, output

    suite, _ = run(root, "--run=fixture_lifetime/worker_exception", 1, failures=1)
    assert "owner failed after starting worker" in case(suite, "worker_exception").findtext("failure", ""), ET.tostring(suite)


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as directory:
        main(Path(directory))
