#!/usr/bin/env python3
"""Validate the public reports produced by the measured, metadata, and recording examples."""

import argparse
import json
import math
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET


SMOKE_ARGS = [
    "--bench-epochs=3", "--bench-warmup=1",
    "--bench-min-epoch-time-s=0.0001", "--bench-min-total-time-s=0",
    "--bench-max-total-time-s=0.02",
]


def capture(executable, *args):
    return subprocess.run(
        [str(executable), *args, "--no-color"], check=True,
        capture_output=True, text=True, encoding="utf-8", timeout=60,
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
        capture_output=True, text=True, encoding="utf-8", timeout=60,
    )
    assert mixed.returncode != 0
    assert "requires a measured-only selection" in mixed.stderr


def check_recording(executable, output, inventory, *, expect_json=False, expect_cbor=False):
    expected_names = {"recording/sum", "recording/throughput", "recording/latency"}
    assert len(inventory) == len(expected_names)
    assert {item["name"] for item in inventory} == expected_names
    expected_content_types = {"application/octet-stream"}
    if expect_json:
        expected_content_types.add("application/json")
    if expect_cbor:
        expected_content_types.add("application/cbor")
    root = output / "records"
    junit = output / "results.xml"
    before = set(root.glob("run-*/index.json"))
    capture(executable, f"--records={root}", f"--junit={junit}", *SMOKE_ARGS)
    index_path, = set(root.glob("run-*/index.json")) - before
    index = json.loads(index_path.read_text(encoding="utf-8"))
    assert index["schemaVersion"] == 1 and not index["errors"]
    assert index["run"]["properties"]["device"] == "simulator"
    assert len(index["cases"]) == 3
    assert {case["name"] for case in index["cases"]} == expected_names
    for case in index["cases"]:
        assert case["outcome"] == "pass"
        props = case["data"]["properties"]
        assert props["sample_count"] == 4 and props["teardown_complete"] is True
        records = case["data"]["records"]
        actual_content_types = {record["contentType"] for record in records}
        assert actual_content_types == expected_content_types, (case["name"], actual_content_types, expected_content_types)
        assert len(records) == len(expected_content_types), (case["name"], "duplicate recording format")
        for record in records:
            data = (index_path.parent / record["path"]).read_bytes()
            if record["contentType"] == "application/json":
                assert json.loads(data) == {"device": "simulator", "samples": [1, 2, 3, 4]}
            elif record["contentType"] == "application/cbor":
                assert data == b"\x82\x69simulator\x84\x01\x02\x03\x04"
            else:
                assert record["contentType"] == "application/octet-stream"
                assert data == bytes([1, 2, 3, 4])
    junit_cases = ET.parse(junit).getroot().findall("testcase")
    assert len(junit_cases) == len(expected_names)
    assert {case.attrib["name"] for case in junit_cases} == expected_names
    for case in junit_cases:
        assert case.find("failure") is None
        props = {p.attrib["name"]: p.attrib["value"] for p in case.findall("properties/property")}
        assert props["gentest.property.device"] == "simulator"
        assert props["gentest.property.teardown_complete"] == "true"
        assert (junit.parent / props["gentest.records"]).resolve() == index_path.resolve()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("example", choices=("metadata", "recording", "measured"))
    parser.add_argument("output", type=Path)
    parser.add_argument("--expect-json", action="store_true", help="require the recording example's JSON payloads")
    parser.add_argument("--expect-cbor", action="store_true", help="require the recording example's CBOR payloads")
    args = parser.parse_args()
    if args.example != "recording" and (args.expect_json or args.expect_cbor):
        parser.error("serializer expectations apply only to the recording example")
    executable = args.executable.resolve()
    example = args.example
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    raw = capture(executable, "--list-json")
    (output / "inventory.json").write_text(raw, encoding="utf-8")
    inventory = json.loads(raw)
    if example == "metadata":
        check_metadata(executable, output, inventory)
    elif example == "recording":
        check_recording(executable, output, inventory, expect_json=args.expect_json, expect_cbor=args.expect_cbor)
    elif example == "measured":
        check_measured(executable, output, inventory)
    else:
        raise ValueError(f"Unsupported example: {example}")


if __name__ == "__main__":
    main()
