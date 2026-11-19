"""Check measured exits, samples, and written reports for each public runner invocation."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

from target_command import target_command


def check_case(binary, root, name, kind, diagnostic, allure_enabled, *, min_epoch=0, expected_samples=None):
    case = f"measured_outcomes/{name}"
    records = root / "records"
    junit = root / "result.xml"
    allure = root / "allure"
    extra = ["--allure-dir=" + str(allure)] if allure_enabled else []
    run = subprocess.run(target_command(
        binary, "--run=" + case, "--report-format=json", "--junit=" + str(junit), "--records=" + str(records),
        "--bench-epochs=3", "--bench-warmup=0", f"--bench-min-epoch-time-s={min_epoch}",
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
            assert diagnostic in result["statusDetails"]["message"], result

    if expected_samples is not None:
        for key, value in (("attempted_calls", 4), ("completed_samples", expected_samples)):
            prop = occurrence.find(f"properties/property[@name='gentest.property.{key}']")
            assert prop is not None and prop.get("value") == str(value), (case, key, ET.tostring(occurrence))

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

    for kind in ("bench", "jitter"):
        # With single-call epochs, call 4 follows calibration and two valid samples.
        # A large target instead puts call 4 first in a four-call calibration batch.
        for skip_after, min_epoch, samples in (("true", 0, 2), ("false", 0, 2), ("false", 1e9, 0)):
            with tempfile.TemporaryDirectory() as directory:
                check_case(binary, Path(directory), f"{kind}_deferred({skip_after})", kind, mismatch,
                           allure_flag == "1", min_epoch=min_epoch, expected_samples=samples)
