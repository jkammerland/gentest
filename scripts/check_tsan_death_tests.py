#!/usr/bin/env python3
"""Require a nonempty, enabled death-test inventory in a configured TSan build."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys


def enabled_death_tests(inventory: dict) -> list[str]:
    tests = inventory.get("tests") if isinstance(inventory, dict) else None
    if not isinstance(tests, list) or not tests:
        raise ValueError("TSan must configure a nonempty death-test inventory")

    names = []
    for test in tests:
        if not isinstance(test, dict) or not isinstance(test.get("name"), str) or not test["name"]:
            raise ValueError("Malformed CTest death-test entry")
        properties = test.get("properties", [])
        if not isinstance(properties, list) or any(not isinstance(prop, dict) for prop in properties):
            raise ValueError("Malformed CTest death-test properties")
        for prop in properties:
            if prop.get("name") == "DISABLED":
                if type(prop.get("value")) is not bool:
                    raise ValueError("Malformed CTest DISABLED property")
                if prop["value"]:
                    raise ValueError("TSan death test is disabled: " + test["name"])
        names.append(test["name"])
    return names


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True)
    parser.add_argument("--config", default="")
    parser.add_argument("--ctest", default="ctest")
    args = parser.parse_args()
    command = [args.ctest, "--test-dir", args.build_dir, "--show-only=json-v1", "-L", "^death$"]
    if args.config:
        command += ["-C", args.config]
    try:
        result = subprocess.run(command, capture_output=True, text=True, check=True)
        names = enabled_death_tests(json.loads(result.stdout))
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        return 1
    print(f"TSan configures {len(names)} enabled death tests")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
