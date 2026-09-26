#!/usr/bin/env python3
"""Prove that installed-example validation rejects missing serializer outputs."""

import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

from check_example_reports import check_recording


class RecordingReportChecks(unittest.TestCase):
    def check_fixture(self, formats, *, expect_json=False, expect_cbor=False, omit_case=None, duplicate=False, empty_junit=False):
        names = ["recording/sum", "recording/throughput", "recording/latency"]
        payloads = {
            "raw": ("application/octet-stream", bytes([1, 2, 3, 4])),
            "json": ("application/json", b'{"device":"simulator","samples":[1,2,3,4]}'),
            "cbor": ("application/cbor", b"\x82\x69simulator\x84\x01\x02\x03\x04"),
        }
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)

            def write_reports(*_args):
                bundle = output / "records" / "run-fixture"
                bundle.mkdir(parents=True)
                cases = []
                junit = ET.Element("testsuite")
                for name in names:
                    records = []
                    for format_name in formats:
                        if (name, format_name) == omit_case:
                            continue
                        mime, payload = payloads[format_name]
                        path = f"{name.rsplit('/', 1)[-1]}.{format_name}"
                        (bundle / path).write_bytes(payload)
                        records.append({"contentType": mime, "path": path})
                    if duplicate:
                        records.append(records[0].copy())
                    cases.append({"name": name, "outcome": "pass", "data": {
                        "properties": {"sample_count": 4, "teardown_complete": True}, "records": records,
                    }})
                    if not empty_junit:
                        case = ET.SubElement(junit, "testcase", name=name)
                        props = ET.SubElement(case, "properties")
                        for key, value in {
                            "gentest.property.device": "simulator",
                            "gentest.property.teardown_complete": "true",
                            "gentest.records": "records/run-fixture/index.json",
                        }.items():
                            ET.SubElement(props, "property", name=key, value=value)
                index = {"schemaVersion": 1, "errors": [], "run": {"properties": {"device": "simulator"}}, "cases": cases}
                (bundle / "index.json").write_text(json.dumps(index), encoding="utf-8")
                ET.ElementTree(junit).write(output / "results.xml", encoding="utf-8")
                return ""

            with patch("check_example_reports.capture", side_effect=write_reports) as capture:
                check_recording("unused-executable", output, [{"name": name} for name in names],
                                expect_json=expect_json, expect_cbor=expect_cbor)
                capture.assert_called_once()

    def test_accepts_each_configured_serializer_combination(self):
        for expect_json in (False, True):
            for expect_cbor in (False, True):
                with self.subTest(json=expect_json, cbor=expect_cbor):
                    formats = ["raw"] + (["json"] if expect_json else []) + (["cbor"] if expect_cbor else [])
                    self.check_fixture(formats, expect_json=expect_json, expect_cbor=expect_cbor)

    def test_rejects_raw_only_when_serializers_enabled(self):
        with self.assertRaises(AssertionError):
            self.check_fixture(["raw"], expect_json=True, expect_cbor=True)

    def test_requires_each_serializer_in_each_case(self):
        for name in ("recording/sum", "recording/throughput", "recording/latency"):
            for format_name in ("json", "cbor"):
                with self.subTest(case=name, format=format_name), self.assertRaises(AssertionError):
                    self.check_fixture(["raw", "json", "cbor"], expect_json=True, expect_cbor=True,
                                       omit_case=(name, format_name))

    def test_rejects_duplicate_payloads(self):
        with self.assertRaises(AssertionError):
            self.check_fixture(["raw"], duplicate=True)

    def test_rejects_empty_junit(self):
        with self.assertRaises(AssertionError):
            self.check_fixture(["raw"], empty_junit=True)


if __name__ == "__main__":
    unittest.main()
