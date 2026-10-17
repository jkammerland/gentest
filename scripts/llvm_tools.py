#!/usr/bin/env python3
"""Resolve repository tooling from the required LLVM major, rejecting older tools."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


POLICY = json.loads(Path(__file__).with_name("llvm_toolchain.json").read_text(encoding="utf-8"))
LLVM_MAJOR = POLICY["major"]


def check_tool(executable: str) -> str:
    resolved = shutil.which(executable)
    if not resolved:
        raise ValueError(f"LLVM {LLVM_MAJOR} tool not found: {executable}")

    version = subprocess.check_output([resolved, "--version"], text=True, stderr=subprocess.STDOUT)
    match = re.search(r"(?:clang(?:-(?:format|tidy|scan-deps))?|LLVM) version\s+(\d+)\b", version)
    if not match:
        match = re.match(r"\s*(\d+)\.", version)
    if not match or int(match[1]) != LLVM_MAJOR:
        raise ValueError(f"Expected LLVM {LLVM_MAJOR}: {resolved}\n{version.strip()}")
    return resolved


def resolve_tool(name: str, override: str = "") -> str:
    # An explicit override must match the policy; do not hide a stale selection.
    variable = {"clang-tidy": "CLANG_TIDY_BIN", "clang-format": "CLANG_FORMAT_BIN"}.get(name, "")
    override = override or os.environ.get(variable, "")
    if override:
        return check_tool(override)

    prefix = os.environ.get("GENTEST_LLVM_PREFIX", "")
    if prefix:
        return check_tool(str(Path(prefix) / "bin" / name))
    candidates = [f"{name}-{LLVM_MAJOR}", name]
    errors = []
    for candidate in candidates:
        try:
            return check_tool(candidate)
        except (ValueError, OSError, subprocess.CalledProcessError) as error:
            errors.append(str(error))
    raise ValueError("\n".join(errors))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    policy = commands.add_parser("policy")
    policy.add_argument("field", choices=POLICY)
    resolve = commands.add_parser("resolve")
    resolve.add_argument("tool")
    resolve.add_argument("override", nargs="?", default="")
    check = commands.add_parser("check")
    check.add_argument("tools", nargs="+")
    args = parser.parse_args()

    try:
        if args.command == "policy":
            print(POLICY[args.field])
        elif args.command == "resolve":
            print(resolve_tool(args.tool, args.override))
        else:
            for executable in args.tools:
                print(f"LLVM {LLVM_MAJOR}: {check_tool(executable)}")
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
