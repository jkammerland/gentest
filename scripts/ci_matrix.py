#!/usr/bin/env python3
"""Select the CMake matrix before GitHub creates platform jobs."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


MATRIX_FILE = Path(__file__).with_name("ci_matrix.json")
PLATFORMS = ("linux", "windows", "macos")


def select_matrix(profile: str, source: Path = MATRIX_FILE) -> dict[str, dict]:
    if profile not in {"pr", "full"}:
        raise ValueError(f"Unknown CI profile: {profile}")
    catalog = json.loads(source.read_text(encoding="utf-8"))
    result = {}
    for platform in PLATFORMS:
        entries = []
        for original in catalog[platform]:
            if profile == "pr" and not original.get("routine", False):
                continue
            entry = {key: value for key, value in original.items() if key != "routine"}
            if profile == "pr":
                entry["enable_package_tests"] = "OFF"
            entries.append(entry)
        if not entries:
            raise ValueError(f"Empty {platform} matrix for {profile}")
        result[platform] = {"include": entries}
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=("pr", "full"), default="pr")
    parser.add_argument("--github-output", type=Path)
    args = parser.parse_args()
    matrices = select_matrix(args.profile)
    if args.github_output:
        with args.github_output.open("a", encoding="utf-8", newline="\n") as output:
            for platform, matrix in matrices.items():
                output.write(f"{platform}={json.dumps(matrix, separators=(',', ':'))}\n")
    else:
        print(json.dumps(matrices, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
