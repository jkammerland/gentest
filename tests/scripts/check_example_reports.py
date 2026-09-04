#!/usr/bin/env python3
"""Validate the public reports produced by the measured and metadata examples."""

import json
import math
from pathlib import Path
import subprocess
import sys
import xml.etree.ElementTree as ET


SMOKE_ARGS = [
    "--bench-epochs=3", "--bench-warmup=1",
    "--bench-min-epoch-time-s=0.0001", "--bench-min-total-time-s=0",
    "--bench-max-total-time-s=0.02",
]


def capture(executable, *args):
    return subprocess.run(
        [str(executable), *args, "--no-color"], check=True,
        capture_output=True, text=True, timeout=60,
    ).stdout


def check_metadata(executable, output, inventory):
    expected = {"metadata/bounded_value(-1)", "metadata/bounded_value(11)"}
    assert {item["name"] for item in inventory} == expected | {"metadata/plain"}
    for item in inventory:
        assert item["kind"] == "test"
        if item["name"] in expected:
            assert item["owner"] == "examples"
            assert item["requirements"] == ["LIMIT-001"]
            assert "fast" in item["tags"]
        else:
            assert item["owner"] == ""
            assert item["requirements"] == []
            assert item["tags"] == []

    junit = output / "junit.xml"
    capture(executable, f"--junit={junit}")
    cases = ET.parse(junit).getroot().findall(".//testcase")
    assert {case.attrib["name"] for case in cases} == expected | {"metadata/plain"}
    for case in cases:
        assert case.find("failure") is None
        requirements = [prop.attrib["value"] for prop in case.findall("properties/property")
                        if prop.attrib["name"] == "requirement"]
        assert requirements == (["LIMIT-001"] if case.attrib["name"] in expected else [])


def check_measured(executable, output, inventory):
    assert len(inventory) == 12
    for kind in ("test", "bench", "jitter"):
        cases = [item for item in inventory if item["kind"] == kind]
        assert len(cases) == 4
        assert all(item["owner"] == "examples" for item in cases)
        if kind == "test":
            assert all(item["requirements"] == ["SUM-001"] for item in cases)
            continue
        assert all(item["itemsPerCall"] == 64 for item in cases)
        raw = capture(executable, f"--kind={kind}", "--report-format=json", *SMOKE_ARGS)
        (output / f"{kind}.json").write_text(raw, encoding="utf-8")
        report = json.loads(raw)
        assert report["issues"] == []
        # Summary rows expose normalized metrics; debug/histogram tables do not.
        rows = [row for table in report["tables"] for row in table["rows"]
                if "median_ns_per_item" in row]
        assert {row["benchmark"] for row in rows} == {item["name"] for item in cases}
        assert len(rows) == 4
        for row in rows:
            assert row["items_per_call"] == 64
            assert row["samples"] > 0
            assert math.isfinite(row["median_ns_per_item"])
            assert row["median_ns_per_item"] >= 0

    # Mixed correctness/measured selection must not masquerade as a machine report.
    mixed = subprocess.run(
        [str(executable), "--report-format=json", *SMOKE_ARGS],
        capture_output=True, text=True, timeout=60,
    )
    assert mixed.returncode != 0
    assert "requires a measured-only selection" in mixed.stderr


def main():
    executable = Path(sys.argv[1]).resolve()
    example = sys.argv[2]
    output = Path(sys.argv[3]).resolve()
    output.mkdir(parents=True, exist_ok=True)
    raw = capture(executable, "--list-json")
    (output / "inventory.json").write_text(raw, encoding="utf-8")
    inventory = json.loads(raw)
    if example == "metadata":
        check_metadata(executable, output, inventory)
    elif example == "measured":
        check_measured(executable, output, inventory)
    else:
        raise ValueError(f"Unsupported example: {example}")


if __name__ == "__main__":
    main()
