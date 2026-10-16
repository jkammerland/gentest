#!/usr/bin/env python3
"""Resolve repository tooling from the required LLVM major, rejecting older tools."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


POLICY = json.loads(Path(__file__).with_name("llvm_toolchain.json").read_text(encoding="utf-8"))
LLVM_MAJOR = POLICY["major"]
TOOL_OVERRIDES = {
    "clang": "GENTEST_CLANG", "clang++": "GENTEST_CLANGXX",
    "clang-tidy": "CLANG_TIDY_BIN", "clang-format": "CLANG_FORMAT_BIN",
    "clang-scan-deps": "GENTEST_CODEGEN_CLANG_SCAN_DEPS",
    "llvm-cov": "GENTEST_LLVM_COV", "llvm-config": "GENTEST_LLVM_CONFIG",
}


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
    variable = TOOL_OVERRIDES.get(name, "")
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


def package_major(directory: Path, name: str) -> int:
    config = directory / f"{name}Config.cmake"
    version = directory / f"{name}ConfigVersion.cmake"
    if not config.is_file() or not version.is_file():
        raise ValueError(f"Missing {name} package configuration/version in {directory}")
    match = re.search(r'set\s*\(\s*PACKAGE_VERSION\s+"?(\d+)\.', version.read_text(encoding="utf-8"), re.I)
    if not match:
        raise ValueError(f"Cannot determine {name} package major in {version}")
    return int(match[1])


@dataclass(frozen=True)
class Toolchain:
    tools: dict[str, str]
    llvm_dir: str
    clang_dir: str
    host_clang: str = ""

    def environment(self) -> dict[str, str]:
        # Preserve every selected tool directory, including explicit overrides.
        directories = list(dict.fromkeys(str(Path(tool).parent) for tool in self.tools.values()))
        return {"PATH": os.pathsep.join([*directories, os.environ.get("PATH", "")]),
                "LLVM_DIR": self.llvm_dir, "Clang_DIR": self.clang_dir,
                "GENTEST_CODEGEN_HOST_CLANG": self.host_clang or self.tools["clang++"],
                **{variable: self.tools[name] for name, variable in TOOL_OVERRIDES.items()}}


def select_toolchain(*, cc: str = "", cxx: str = "") -> Toolchain:
    """Select host LLVM once; target compilers belong to individual build lanes."""
    overrides = {"clang": cc, "clang++": cxx}
    host_override = os.environ.get("GENTEST_CODEGEN_HOST_CLANG", "")
    host_clang = str(Path(check_tool(host_override)).resolve()) if host_override else ""
    clangxx = Path(resolve_tool("clang++", cxx)).resolve()
    prefix = Path(os.environ.get("GENTEST_LLVM_PREFIX") or clangxx.parent.parent).resolve()
    tools = {}
    for name, variable in TOOL_OVERRIDES.items():
        override = overrides.get(name) or os.environ.get(variable, "")
        if override:
            selected = check_tool(override)
        else:
            # Once a prefix is selected, missing/stale tools cannot escape to PATH.
            candidates = [prefix / "bin" / name, prefix / "bin" / f"{name}-{LLVM_MAJOR}"]
            selected = check_tool(str(next((path for path in candidates if path.is_file()), candidates[0])))
        tools[name] = str(Path(selected).resolve())

    # llvm-config supplies package locations without executing a configure.
    cmakedir = subprocess.check_output([tools["llvm-config"], "--cmakedir"], text=True).strip()
    llvm_dir = Path(os.environ.get("LLVM_DIR") or cmakedir).resolve()
    clang_default = llvm_dir.parent / "clang"
    clang_dir = Path(os.environ.get("Clang_DIR") or clang_default).resolve()
    llvm_major, clang_major = package_major(llvm_dir, "LLVM"), package_major(clang_dir, "Clang")
    if llvm_major != clang_major:
        raise ValueError(f"LLVM and Clang package majors must agree: LLVM {llvm_major}, Clang {clang_major}")
    if llvm_major != LLVM_MAJOR:
        raise ValueError(f"Expected LLVM/Clang {LLVM_MAJOR} packages; found {llvm_major}")
    return Toolchain(tools, str(llvm_dir), str(clang_dir), host_clang or tools["clang++"])


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
    commands.add_parser("check-toolchain")
    args = parser.parse_args()

    try:
        if args.command == "policy":
            print(POLICY[args.field])
        elif args.command == "resolve":
            print(resolve_tool(args.tool, args.override))
        elif args.command == "check":
            for executable in args.tools:
                print(f"LLVM {LLVM_MAJOR}: {check_tool(executable)}")
        else:
            selected = select_toolchain()
            print(f"LLVM {LLVM_MAJOR}: {selected.llvm_dir}; Clang: {selected.clang_dir}")
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
