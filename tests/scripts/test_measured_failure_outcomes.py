"""Check measured exits, samples, and written reports for each public runner invocation."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

from target_command import target_command


def check_case(binary, root, name, kind, diagnostic, allure_enabled):
    case = f"measured_outcomes/{name}"
    records = root / "records"
    junit = root / "result.xml"
    allure = root / "allure"
    extra = ["--allure-dir=" + str(allure)] if allure_enabled else []
    run = subprocess.run(target_command(
        binary, "--run=" + case, "--report-format=json", "--junit=" + str(junit), "--records=" + str(records),
        "--bench-epochs=3", "--bench-warmup=0", "--bench-min-epoch-time-s=0",
        "--bench-min-total-time-s=0", "--bench-max-total-time-s=0", *extra,
    ), capture_output=True, text=True, timeout=30)
    failed = diagnostic is not None
    assert run.returncode == int(failed), (case, run)
    report = json.loads(run.stdout)
    summary, = (table for table in report["tables"] if table["id"] == kind + ".summary")
    rows = summary["rows"]

    # Each writer must preserve the outcome even when a usable timing sample exists.
    suite = ET.parse(junit).getroot()
    occurrence, = suite.iter("testcase")
    assert occurrence.attrib["name"] == case, (case, suite.attrib)
    assert int(suite.attrib["failures"]) == int(failed), (case, suite.attrib)
    index, = records.glob("*/index.json")
    recorded, = json.loads(index.read_text())["cases"]
    assert (recorded["name"], recorded["outcome"]) == (case, "fail" if failed else "pass"), recorded
    if allure_enabled:
        result_file, = allure.glob("*-result.json")
        result = json.loads(result_file.read_text(encoding="utf-8"))
        assert (result["name"], result["status"]) == (case, "failed" if failed else "passed"), result

    if failed:
        issue, = report["issues"]
        failure = occurrence.find("failure")
        assert failure is not None and diagnostic in failure.text and "call failed" in failure.text, (case, failure)
        assert issue["name"] == case and diagnostic in issue["message"] and not rows, (case, report)
    else:
        row, = rows
        assert row["samples"] == 1 and report["issues"] == [], (case, report)
        if name == "supplied_ok":
            assert row["median_ns_per_item"] == 1_000_000, row


if __name__ == "__main__":
    binary, allure_flag = sys.argv[1:]
    assert allure_flag in ("0", "1")
    empty = "measured call failed without a diagnostic"
    mismatch = "argument[0] mismatch"
    for name, kind, diagnostic in (
        ("supplied_empty", "bench", empty),
        ("calibrated_empty", "bench", empty),
        ("jitter_empty", "jitter", empty),
        ("bench_noexcept_mismatch", "bench", mismatch),
        ("jitter_noexcept_mismatch", "jitter", mismatch),
        ("ordinary_mismatch", "bench", mismatch),
        ("noexcept_match", "bench", None),
        ("supplied_ok", "bench", None),
    ):
        with tempfile.TemporaryDirectory() as directory:
            check_case(binary, Path(directory), name, kind, diagnostic, allure_flag == "1")
