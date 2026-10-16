#!/usr/bin/env python3
"""Select the CMake matrix before GitHub creates platform jobs."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from llvm_tools import LLVM_MAJOR, POLICY


MATRIX_FILE = Path(__file__).with_name("ci_matrix.json")
PLATFORMS = ("linux", "windows", "macos")


def select_matrix(source: Path = MATRIX_FILE) -> dict[str, dict]:
    catalog = json.loads(source.read_text(encoding="utf-8"))
    result = {}
    for platform in PLATFORMS:
        entries = []
        for original in catalog[platform]:
            if original.get("primary"):
                if platform == "linux":
                    version = original.get("clang_version")
                elif platform == "windows":
                    version = original.get("llvm-version", "").split(".")[0]
                else:
                    version = original.get("compiler", "").removeprefix("llvm@")
                if str(version) != str(LLVM_MAJOR):
                    raise ValueError(f"Primary {platform} lane must use LLVM {LLVM_MAJOR}: {original}")
                if platform == "windows" and original["llvm-version"] != POLICY["release"]:
                    raise ValueError("Primary Windows lane must use the pinned LLVM release")

            if "routine" in original:
                raise ValueError("Routine CI profiles are no longer supported")

            entries.append(dict(original))
        if not entries:
            raise ValueError(f"Empty {platform} matrix")

        # Windows presets share the explicit RelWithDebInfo configuration.
        if platform == "windows":
            effective = [(entry["llvm-version"], entry["variant"], entry.get("test_cxx_standard")) for entry in entries]
            if len(effective) != len(set(effective)):
                raise ValueError("Duplicate effective Windows configuration")
        result[platform] = {"include": entries}
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--github-output", type=Path)
    args = parser.parse_args()
    matrices = select_matrix()
    if args.github_output:
        with args.github_output.open("a", encoding="utf-8", newline="\n") as output:
            for platform, matrix in matrices.items():
                output.write(f"{platform}={json.dumps(matrix, separators=(',', ':'))}\n")
    else:
        print(json.dumps(matrices, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
