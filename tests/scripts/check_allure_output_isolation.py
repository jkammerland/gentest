#!/usr/bin/env python3
"""Reject configured CTest writers that share an Allure output directory."""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess


def check_inventory(tests):
    owners = {}
    for test in tests:
        arguments = []
        for argument in test.get("command", []):
            if re.match(r"^-DARGS(?::[^=]+)?=", argument):
                # Gentest passes CMake argument lists through one -D value.
                arguments.extend(re.split(r"\\?;", argument.split("=", 1)[1]))
            else:
                arguments.append(argument)

        for index, argument in enumerate(arguments):
            if argument.startswith("--allure-dir="):
                directory = argument.split("=", 1)[1]
            elif argument == "--allure-dir" and index + 1 < len(arguments):
                directory = arguments[index + 1]
            else:
                continue
            if not directory:
                continue

            # Compare directory identity even when parents use aliases or '..'.
            key = os.path.normcase(str(Path(directory).resolve()))
            previous = owners.setdefault(key, test["name"])
            if previous != test["name"]:
                raise ValueError(f"Allure output directory shared by {previous} and {test['name']}: {directory}")

    if not owners:
        raise ValueError("No Allure output directories found in the configured CTest inventory")
    return len(owners)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ctest", required=True)
    parser.add_argument("--build-dir", required=True)
    parser.add_argument("--config", default="", help="CTest configuration for a multi-config build")
    args = parser.parse_args()

    # Multi-config inventories omit test commands until a configuration is selected.
    command = [args.ctest, "--test-dir", args.build_dir, "--show-only=json-v1"]
    if args.config:
        command.extend(["-C", args.config])
    result = subprocess.run(command, check=True, capture_output=True, text=True, encoding="utf-8")
    try:
        count = check_inventory(json.loads(result.stdout)["tests"])
    except ValueError as error:
        parser.exit(1, f"{error}\n")
    print(f"Allure output isolation: {count} independent directories")


if __name__ == "__main__":
    main()
