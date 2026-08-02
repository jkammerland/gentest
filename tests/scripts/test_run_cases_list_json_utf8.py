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

    result = subprocess.run(
        [str(Path(sys.argv[1])), "--list-json-malformed"],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
        raise SystemExit(f"--list-json failed with {result.returncode}: {result.stderr.decode('utf-8', 'replace')}")

    try:
        inventory = json.loads(result.stdout.decode("utf-8", "strict"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise SystemExit(f"--list-json did not produce strict UTF-8 JSON: {error}") from error

    if inventory[0]["name"] != "embed/malformed-\ufffd":
        raise SystemExit(f"malformed input was not replaced in JSON inventory: {inventory!r}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
