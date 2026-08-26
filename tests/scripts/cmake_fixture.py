#!/usr/bin/env python3

"""Small, stdlib-only helpers for CMake fixture regression tests."""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence


@dataclass(frozen=True)
class ConfigureOptions:
    cmake_command: tuple[str, ...]
    source_dir: Path
    build_root: Path
    generator: str
    generator_platform: str | None = None
    generator_toolset: str | None = None
    toolchain_file: str | None = None
    make_program: str | None = None
    c_compiler: str | None = None
    cxx_compiler: str | None = None
    llvm_dir: str | None = None
    clang_dir: str | None = None
    build_type: str | None = None

    @property
    def work_dir(self) -> Path:
        fixture_name = self.source_dir.name
        if not fixture_name:
            raise ValueError(f"fixture source has no basename: {self.source_dir}")
        return self.build_root / fixture_name


def build_configure_argv(options: ConfigureOptions) -> list[str]:
    command = [
        *options.cmake_command,
        "-S",
        str(options.source_dir),
        "-B",
        str(options.work_dir),
        "-G",
        options.generator,
    ]

    for flag, value in (
        ("-A", options.generator_platform),
        ("-T", options.generator_toolset),
    ):
        if value:
            command.extend((flag, value))

    for variable, value in (
        ("CMAKE_TOOLCHAIN_FILE", options.toolchain_file),
        ("CMAKE_MAKE_PROGRAM", options.make_program),
        ("CMAKE_C_COMPILER", options.c_compiler),
        ("CMAKE_CXX_COMPILER", options.cxx_compiler),
        ("LLVM_DIR", options.llvm_dir),
        ("Clang_DIR", options.clang_dir),
        ("CMAKE_BUILD_TYPE", options.build_type),
    ):
        if value:
            command.append(f"-D{variable}={value}")

    return command


def normalize_whitespace(value: str) -> str:
    return re.sub(r"[ \t\r\n]+", " ", value)


def _prepare_work_dir(options: ConfigureOptions) -> None:
    build_root = options.build_root.resolve()
    work_dir = options.work_dir

    if work_dir.is_symlink():
        raise ValueError(f"refusing to remove symlinked fixture work directory: {work_dir}")
    if work_dir.parent.resolve() != build_root or work_dir.resolve() == build_root:
        raise ValueError(f"fixture work directory is not a direct child of build root: {work_dir}")

    if work_dir.exists():
        shutil.rmtree(work_dir)


def check_configure_failure(options: ConfigureOptions, required_substring: str) -> None:
    _prepare_work_dir(options)
    result = subprocess.run(
        build_configure_argv(options),
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    output = f"{result.stdout.rstrip()}\n{result.stderr.rstrip()}"

    if result.returncode == 0:
        raise RuntimeError(f"Expected configure to fail, but exit code was 0. Output:\n{output}")

    if normalize_whitespace(required_substring) not in normalize_whitespace(output):
        raise RuntimeError(
            f"Expected substring not found in output: {required_substring!r}. Output:\n{output}"
        )


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="action", required=True)
    configure_fails = subparsers.add_parser(
        "configure-fails", help="require a fixture configure to fail with a diagnostic"
    )
    configure_fails.add_argument("--cmake-command", required=True)
    configure_fails.add_argument("--source-dir", required=True, type=Path)
    configure_fails.add_argument("--build-root", required=True, type=Path)
    configure_fails.add_argument("--generator", required=True)
    configure_fails.add_argument("--generator-platform")
    configure_fails.add_argument("--generator-toolset")
    configure_fails.add_argument("--toolchain-file")
    configure_fails.add_argument("--make-program")
    configure_fails.add_argument("--c-compiler")
    configure_fails.add_argument("--cxx-compiler")
    configure_fails.add_argument("--llvm-dir")
    configure_fails.add_argument("--clang-dir")
    configure_fails.add_argument("--build-type")
    configure_fails.add_argument("--required-substring", required=True)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    options = ConfigureOptions(
        cmake_command=(args.cmake_command,),
        source_dir=args.source_dir,
        build_root=args.build_root,
        generator=args.generator,
        generator_platform=args.generator_platform,
        generator_toolset=args.generator_toolset,
        toolchain_file=args.toolchain_file,
        make_program=args.make_program,
        c_compiler=args.c_compiler,
        cxx_compiler=args.cxx_compiler,
        llvm_dir=args.llvm_dir,
        clang_dir=args.clang_dir,
        build_type=args.build_type,
    )

    try:
        check_configure_failure(options, args.required_substring)
    except (OSError, RuntimeError, ValueError) as error:
        print(f"cmake fixture check failed: {error}", file=sys.stderr)
        return 1

    print("CMake fixture check passed (configure failed with expected message)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
