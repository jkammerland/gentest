#!/usr/bin/env python3
"""Verify caller-owned invalid UTF-8 is rendered as valid JSON text."""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_run_cases_list_json_utf8.py <run-cases-api-test>")

    expected_name = "embed/malformed-\ufffd-é-€-😀-" + "\ufffd" * 2 + "-" + "\ufffd" * 3 + "-" + "\ufffd" * 4 + "-" + "\ufffd" * 2
    for mode, expected_status in (("--list-json-malformed", 0), ("--measured-json-malformed", 0), ("--measured-json-malformed-failure", 1)):
        result = subprocess.run(
            [str(Path(sys.argv[1])), mode],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        if result.returncode != expected_status:
            raise SystemExit(f"{mode} returned {result.returncode}: {result.stderr.decode('utf-8', 'replace')}")

        try:
            report = json.loads(result.stdout.decode("utf-8", "strict"))
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise SystemExit(f"{mode} did not produce strict UTF-8 JSON: {error}") from error

        if mode == "--list-json-malformed":
            assert len(report) == 1, report
            assert report[0]["name"] == expected_name, report
            assert report[0]["file"] == "malformed-\ufffd.cpp", report
        elif expected_status == 0:
            assert len(report["tables"]) == 2, report
            for table in report["tables"]:
                assert len(table["rows"]) == 1, report
                assert table["rows"][0]["benchmark"] == expected_name, report
        else:
            assert len(report["issues"]) == 1, report
            issue = report["issues"][0]
            assert issue["name"] == expected_name, report
            assert issue["file"] == "malformed-\ufffd.cpp", report
            assert "failure-\ufffd" in issue["message"], report
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
