#!/usr/bin/env python3
"""Shared commands for the recording and alternate-build-system acceptance lanes.

Dependency installation belongs to the caller. This module neither provisions
machines nor publishes evidence; CI and validate_local use the same commands.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass, field
import os
from pathlib import Path
import shutil
import subprocess
import sys


@dataclass
class Command:
    args: list[str]
    cwd: Path
    env: dict[str, str] = field(default_factory=dict)


def suite_commands(suite: str, source: Path, work: Path, *, jobs: int = 2,
                   codegen: Path | None = None, serializers: str = "",
                   cc: str = "clang", cxx: str = "clang++") -> list[Command]:
    source, work = source.resolve(), work.resolve()
    common = {"CC": cc, "CXX": cxx, "CCACHE_DISABLE": "1"}
    if codegen:
        common.update(GENTEST_CODEGEN=str(codegen), GENTEST_CODEGEN_HOST_CLANG=cxx)
    commands: list[Command] = []

    def add(*args: object, env: dict[str, str] | None = None):
        commands.append(Command([str(a) for a in args], source, common | (env or {})))

    def downstream(script: str):
        args = ["cmake", f"-DSOURCE_DIR={source}", f"-DBUILD_ROOT={work / 'downstream'}"]
        if codegen:
            args.append(f"-DPROG={codegen}")
        add(*args, "-P", source / "tests/cmake/scripts" / script)

    def consumer(prefix: list[str]):
        add(*prefix, "--list")
        for case, kind in [("module_test", "test"), ("module_mock", "test"),
                           ("log_sink", "test"), ("module_bench", "bench"), ("module_jitter", "jitter")]:
            add(*prefix, f"--run=consumer/consumer/{case}", f"--kind={kind}")

    if suite == "recording":
        if not serializers:
            raise ValueError("recording needs --serializers (installed Glaze and cbor_tags prefix)")
        build, install, example = work / "build", work / "install", work / "example"
        add("cmake", "--preset=debug-system", "-B", build,
            f"-DCMAKE_C_COMPILER={cc}", f"-DCMAKE_CXX_COMPILER={cxx}",
            f"-DCMAKE_PREFIX_PATH={serializers}", "-DGENTEST_ENABLE_RECORDING_ADAPTER_TESTS=ON",
            "-DGENTEST_ENABLE_PACKAGE_TESTS=OFF", "-DGENTEST_USE_BOOST_JSON=ON",
            "-Dgentest_INSTALL=ON", f"-DCMAKE_INSTALL_PREFIX={install}")
        add("cmake", "--build", build, "--parallel", jobs, "--target",
            "gentest_recording_tests", "gentest_recording_lifetime_tests", "gentest_recording_shared_payloads_tests",
            "gentest_record_json_tests", "gentest_record_cbor_tests", "gentest_record_json_noexceptions", "gentest_main")
        add("ctest", "--test-dir", build, "--output-on-failure", "--no-tests=error",
            "--output-junit", work / "recording.xml", "-R", "^runtime_record")
        add("cmake", "--install", build)
        add("cmake", "-S", source / "examples/recording", "-B", example, "-G", "Ninja",
            f"-DCMAKE_CXX_COMPILER={cxx}", f"-DCMAKE_PREFIX_PATH={install};{serializers}",
            "-DRECORDING_WITH_GLAZE=ON", "-DRECORDING_WITH_CBOR=ON")
        add("cmake", "--build", example, "--parallel", jobs)
        add("ctest", "--test-dir", example, "--output-on-failure", "--no-tests=error",
            "--output-junit", work / "example.xml")
        add(sys.executable, source / "tests/scripts/check_example_reports.py",
            example / ("gentest_recording.exe" if os.name == "nt" else "gentest_recording"),
            "recording", example / "reports", "--expect-json", "--expect-cbor")
    elif suite == "bazel":
        common["GENTEST_BAZEL_LOCAL_CLANG"] = cxx
        bazel = ["bazel", f"--output_user_root={work / 'cache'}"]
        flags = [f"--symlink_prefix={work / 'bazel-'}", f"--jobs={jobs}",
                 "--action_env=CCACHE_DISABLE=1", "--host_action_env=CCACHE_DISABLE=1",
                 "--action_env=CC", "--action_env=CXX", "--host_action_env=CC", "--host_action_env=CXX",
                 "--repo_env=CC", "--repo_env=CXX", "--repo_env=GENTEST_BAZEL_LOCAL_CLANG"]
        add(*bazel, "test", *flags, "--test_output=errors", "//:gentest_unit_bazel", "//:gentest_integration_bazel",
            "//:gentest_fixtures_bazel", "//:gentest_skiponly_bazel", "//:codegen_check_invalid")
        for style in ["textual", "module"]:
            name = f"gentest_consumer_{style}_bazel"
            add(*bazel, "build", *flags, *(["--experimental_cpp_modules"] if style == "module" else []), f"//:{name}")
            consumer([str(work / "bazel-bin" / name)])
        downstream("CheckBazelBzlmodConsumer.cmake")
    elif suite == "meson":
        if not codegen:
            raise ValueError("meson needs --codegen")
        add("meson", "setup", work / "build", f"-Dcodegen_path={codegen}", f"-Dcodegen_host_clang={cxx}")
        add("meson", "compile", "-C", work / "build", "-j", jobs)
        add("meson", "test", "-C", work / "build", "--print-errorlogs")
        downstream("CheckMesonWrapConsumer.cmake")
    elif suite == "xmake":
        if not codegen:
            raise ValueError("xmake needs --codegen")
        add("xmake", "f", "-c", "-y", "-m", "release", "-o", work / "build")
        add("xmake", "build", "-y", "-j", jobs)
        for name in ["unit", "integration", "fixtures", "skiponly"]:
            add("xmake", "r", "-y", f"gentest_{name}_xmake")
        for style in ["textual", "module"]:
            consumer(["xmake", "r", "-y", f"gentest_consumer_{style}_xmake"])
        downstream("CheckXmakeXrepoConsumer.cmake")
    else:
        raise ValueError(f"Unknown acceptance suite: {suite}")
    return commands


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("suite", choices=["recording", "bazel", "meson", "xmake"])
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--codegen", type=Path)
    parser.add_argument("--serializers", default=os.environ.get("GENTEST_SERIALIZER_PREFIX", ""))
    parser.add_argument("--cc", default=os.environ.get("CC", "clang"))
    parser.add_argument("--cxx", default=os.environ.get("CXX", "clang++"))
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    args.work.mkdir(parents=True, exist_ok=True)
    commands = suite_commands(args.suite, args.source, args.work, jobs=args.jobs, codegen=args.codegen,
                              serializers=args.serializers, cc=shutil.which(args.cc) or args.cc,
                              cxx=shutil.which(args.cxx) or args.cxx)
    for command in commands:
        print("+", subprocess.list2cmdline(command.args), flush=True)
        subprocess.run(command.args, cwd=command.cwd, env=os.environ | command.env, check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
