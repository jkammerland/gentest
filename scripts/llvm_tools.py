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
HOST_TOOLS = ("clang", "clang++", "clang-scan-deps", "llvm-config")


def check_tool(executable: str, *, major: int = LLVM_MAJOR) -> str:
    resolved = shutil.which(executable)
    if not resolved:
        raise ValueError(f"LLVM {major} tool not found: {executable}")

    version = subprocess.check_output([resolved, "--version"], text=True, stderr=subprocess.STDOUT)
    match = re.search(r"(?:clang(?:-(?:format|tidy|scan-deps))?|LLVM) version\s+(\d+)\b", version)
    if not match:
        match = re.match(r"\s*(\d+)\.", version)
    if not match or int(match[1]) != major:
        raise ValueError(f"Expected LLVM {major}: {resolved}\n{version.strip()}")
    return resolved


def resolve_tool(name: str, override: str = "", *, major: int = LLVM_MAJOR) -> str:
    # An explicit override must match the policy; do not hide a stale selection.
    variable = TOOL_OVERRIDES.get(name, "")
    override = override or os.environ.get(variable, "")
    if override:
        return check_tool(override, major=major)

    prefix = os.environ.get("GENTEST_LLVM_PREFIX", "")
    if prefix:
        return check_tool(str(Path(prefix) / "bin" / name), major=major)
    candidates = [f"{name}-{major}", name]
    errors = []
    for candidate in candidates:
        try:
            return check_tool(candidate, major=major)
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
    llvm_dir: str = ""
    clang_dir: str = ""
    host_clang: str = ""

    def environment(self) -> dict[str, str]:
        # Preserve every selected tool directory, including explicit overrides.
        directories = list(dict.fromkeys(str(Path(tool).parent) for tool in self.tools.values()))
        environment = {"PATH": os.pathsep.join([*directories, os.environ.get("PATH", "")]),
                       **{TOOL_OVERRIDES[name]: tool for name, tool in self.tools.items()}}
        if self.llvm_dir:
            environment.update(LLVM_DIR=self.llvm_dir, Clang_DIR=self.clang_dir)
        if self.host_clang:
            environment["GENTEST_CODEGEN_HOST_CLANG"] = self.host_clang
        return environment


def select_toolchain(*, required: tuple[str, ...] = tuple(TOOL_OVERRIDES), packages: bool = True,
                     major: int = LLVM_MAJOR, cc: str = "", cxx: str = "") -> Toolchain:
    """Select a role's tools; a different major is an explicit compatibility lane.

    cc/cxx select LLVM host drivers. Target compilers (including GCC, MSVC,
    AppleClang and cross compilers) remain separate caller inputs.
    """
    unknown = set(required) - TOOL_OVERRIDES.keys()
    if unknown:
        raise ValueError(f"Unknown LLVM tools: {sorted(unknown)}")
    names = tuple(dict.fromkeys((*required, *(('clang++', 'llvm-config') if packages else ()))))
    overrides = {"clang": cc, "clang++": cxx}
    host_override = os.environ.get("GENTEST_CODEGEN_HOST_CLANG", "") if packages else ""
    host_clang = str(Path(check_tool(host_override, major=major)).absolute()) if host_override else ""
    # Resolve the installation for package discovery, but preserve executable
    # names below: clang++ and compiler wrappers depend on the invoked basename.
    prefix_value = os.environ.get("GENTEST_LLVM_PREFIX", "")
    if not prefix_value and "clang++" in names:
        driver = cxx or os.environ.get("GENTEST_CLANGXX", "") or host_clang
        prefix_value = str(Path(resolve_tool("clang++", driver, major=major)).resolve().parent.parent)
    prefix = Path(prefix_value).resolve() if prefix_value else None
    tools = {}
    for name in names:
        variable = TOOL_OVERRIDES[name]
        override = overrides.get(name) or os.environ.get(variable, "")
        if override:
            selected = check_tool(override, major=major)
        elif prefix:
            # Once a prefix is selected, missing/stale tools cannot escape to PATH.
            candidates = [prefix / "bin" / name, prefix / "bin" / f"{name}-{major}"]
            selected = check_tool(str(next((path for path in candidates if path.is_file()), candidates[0])), major=major)
        else:
            selected = resolve_tool(name, major=major)
        tools[name] = str(Path(selected).absolute())

    if not packages:
        return Toolchain(tools)

    # llvm-config supplies package locations without executing a configure.
    cmakedir = subprocess.check_output([tools["llvm-config"], "--cmakedir"], text=True).strip()
    llvm_dir = Path(os.environ.get("LLVM_DIR") or cmakedir).resolve()
    clang_default = llvm_dir.parent / "clang"
    clang_dir = Path(os.environ.get("Clang_DIR") or clang_default).resolve()
    llvm_major, clang_major = package_major(llvm_dir, "LLVM"), package_major(clang_dir, "Clang")
    if llvm_major != clang_major:
        raise ValueError(f"LLVM and Clang package majors must agree: LLVM {llvm_major}, Clang {clang_major}")
    if llvm_major != major:
        raise ValueError(f"Expected LLVM/Clang {major} packages; found {llvm_major}")
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
    toolchain = commands.add_parser("check-toolchain")
    toolchain.add_argument("--tools", nargs="+", choices=TOOL_OVERRIDES, default=tuple(TOOL_OVERRIDES))
    toolchain.add_argument("--without-packages", action="store_true")
    toolchain.add_argument("--major", type=int, default=LLVM_MAJOR, help="Explicit LLVM compatibility lane")
    toolchain.add_argument("--github-env", type=Path, help="Export the validated selection for subsequent Actions steps")
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
            selected = select_toolchain(required=tuple(args.tools), packages=not args.without_packages, major=args.major)
            if args.github_env:
                environment = selected.environment()
                if any("\n" in value or "\r" in value for value in environment.values()):
                    raise ValueError("Tool environment values must be single lines")
                with args.github_env.open("a", encoding="utf-8") as output:
                    for name, value in environment.items():
                        output.write(f"{name}={value}\n")
            print(f"LLVM {args.major}: " + "; ".join(f"{name}={path}" for name, path in selected.tools.items()))
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
