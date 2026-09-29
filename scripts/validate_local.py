#!/usr/bin/env python3
"""Run private native validation, or combine already-local reports offline.

No host inventory, remote transport, upload, GitHub API, or telemetry is built in.
Build tools may download normal project dependencies. collect only reads files.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass, field
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import signal
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

from validation_suites import Command, suite_commands

ROOT = Path(__file__).resolve().parents[1]
SCHEMA = 1
HOSTS = ("Linux", "Windows", "Darwin")
SHA = re.compile(r"(?:[0-9a-f]{40}|[0-9a-f]{64})\Z")


def required_stages(host: str) -> list[str]:
    if host == "Linux":
        return ["clang-debug", "clang-release", "gcc-release", "asan-ubsan", "tsan", "coverage",
                "package", "host-codegen", "aarch64-qemu", "riscv64-qemu", "bazel", "meson", "xmake",
                "recording", "measured", "lint"]
    if host == "Windows":
        return [f"{compiler}-{config}" for compiler in ("clang", "msvc") for config in ("debug", "release")]
    if host == "Darwin":
        return [f"{compiler}-{config}" for compiler in ("appleclang", "llvm") for config in ("debug", "release")]
    raise ValueError(f"Unsupported host: {host}")


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def validator_hashes() -> dict[str, str]:
    return {name: sha256(Path(__file__).with_name(name)) for name in ("validate_local.py", "validation_suites.py")}


def git(source: Path, *args: str) -> str:
    return subprocess.check_output(["git", "-C", str(source), *args], text=True, stderr=subprocess.PIPE).strip()


def verify_source(source: Path, commit: str) -> str:
    resolved = git(source, "rev-parse", "--verify", commit + "^{commit}")
    if git(source, "rev-parse", "HEAD") != resolved:
        raise ValueError("Source HEAD does not match --commit")
    if git(source, "status", "--porcelain", "--untracked-files=no"):
        raise ValueError("Source has tracked modifications; commit or isolate them before validation")
    return resolved


def private_directory(path: Path, source: Path | None = None) -> Path:
    path = path.resolve()
    if source is not None and path.is_relative_to(source.resolve()):
        raise ValueError("Evidence must be outside the source checkout")
    # Do not silently overwrite an earlier run. On Windows permissions inherit
    # the user's selected private parent; on POSIX restrict newly created files.
    path.mkdir(mode=0o700, parents=True, exist_ok=False)
    return path


@dataclass
class Stage:
    name: str
    commands: list[Command]
    tools: list[str] = field(default_factory=list)
    dependencies: list[str] = field(default_factory=list)
    missing: str = ""


def make_plan(host: str, source: Path, output: Path, base: Path, jobs: int) -> list[Stage]:
    stages: list[Stage] = []
    presets = json.loads((source / "CMakePresets.json").read_text(encoding="utf-8"))
    by_name = {p["name"]: p for p in presets["configurePresets"]}
    clang = os.environ.get("GENTEST_CLANG", "clang")
    clangxx = os.environ.get("GENTEST_CLANGXX", "clang++")
    if host == "Darwin":
        # Homebrew's LLVM is keg-only. An explicit prefix is private runtime
        # configuration, never a hostname/path inventory in this repository.
        prefix = os.environ.get("GENTEST_LLVM_PREFIX", "")
        if prefix:
            clang, clangxx = str(Path(prefix) / "bin/clang"), str(Path(prefix) / "bin/clang++")
    clang, clangxx = shutil.which(clang) or clang, shutil.which(clangxx) or clangxx

    def cmake_stage(name: str, preset: str, cc: str, cxx: str, *, origin: Path = source,
                    extra: tuple[str, ...] = (), targets: tuple[str, ...] = (), tests: bool = True,
                    cmake: str = "cmake") -> Stage:
        build = output / "work" / name
        # Generic --build does not inherit preset environment, so preserve it
        # explicitly for instrumented code generators as well as test programs.
        env = {"GENTEST_HELPER_BUILD_PARALLEL_LEVEL": "1", "CMAKE_BUILD_PARALLEL_LEVEL": str(jobs)}
        env.update(by_name[preset].get("environment", {}))
        tool_dir = Path(cmake).parent
        ctest = str(tool_dir / ("ctest.exe" if host == "Windows" else "ctest")) if tool_dir != Path(".") else "ctest"
        args = [cmake, "--preset=" + preset, "-B", str(build)]
        if cc:
            args += ["-DCMAKE_C_COMPILER=" + cc, "-DCMAKE_CXX_COMPILER=" + cxx]
        args += ["-DGENTEST_ENABLE_PUBLIC_MODULES=AUTO", "-DGENTEST_USE_BOOST_JSON=ON", *extra]
        boost_include = os.environ.get("GENTEST_BOOST_JSON_INCLUDE_DIR", "")
        if boost_include and "-DGENTEST_USE_BOOST_JSON=OFF" not in extra:
            args.append("-DGENTEST_BOOST_JSON_INCLUDE_DIR=" + boost_include)
        commands = [Command(args, origin, env), Command([cmake, "--build", str(build), "--parallel", str(jobs),
                    *(["--target", *targets] if targets else [])], origin, env)]
        if tests:
            commands.append(Command([ctest, "--preset=" + preset, "--test-dir", str(build), "--output-on-failure",
                "--no-tests=error", "--parallel", "1" if name == "coverage" else str(jobs),
                "--output-junit", str(output / "evidence" / (name + ".xml"))], origin, env))
        stage = Stage(name, commands, [cmake, ctest, "ninja", *([cc, cxx] if cc else [])])
        stages.append(stage)
        return stage

    if host == "Linux":
        for config in ("debug", "release"):
            cmake_stage("clang-" + config, config + "-system", clang, clangxx)
        cmake_stage("gcc-release", "release-system", os.environ.get("GENTEST_GCC", "gcc"), os.environ.get("GENTEST_GXX", "g++"))
        cmake_stage("asan-ubsan", "alusan-system", clang, clangxx)
        cmake_stage("tsan", "tsan-system", clang, clangxx)
        coverage = cmake_stage("coverage", "coverage-system", clang, clangxx)
        coverage.tools += ["gcovr", "llvm-cov"]
        build = output / "work/coverage"
        coverage.commands += [
            Command([sys.executable, str(source / "scripts/coverage_hygiene.py"), "--build-dir", str(build),
                     "--ignore-statuses", "stamp_mismatch", "--gcov", "llvm-cov", "gcov"], source),
            Command([sys.executable, str(source / "scripts/coverage_report.py"), "--build-dir", str(build)], source)]
        package_cmake = os.environ.get("GENTEST_PACKAGE_CMAKE", "cmake")
        package = cmake_stage("package", "release-package", clang, clangxx, cmake=package_cmake)
        package.missing = "VCPKG_ROOT must name a normal vcpkg installation" if not os.environ.get("VCPKG_ROOT") else ""
        cpack = str(Path(package_cmake).with_name("cpack")) if Path(package_cmake).parent != Path(".") else "cpack"
        package.tools.append(cpack)
        package.commands.append(Command([cpack, "--config", str(output / "work/package/CPackConfig.cmake"),
                                         "-B", str(output / "work/package/artifacts")], source))
        cmake_stage("host-codegen", "host-codegen", clang, clangxx, targets=("gentest_codegen",), tests=False)
        codegen = output / "work/host-codegen/tools/gentest_codegen"
        for arch in ("aarch64", "riscv64"):
            name = arch + "-qemu"
            targets = next(p["targets"] for p in presets["buildPresets"] if p["name"] == name)
            cross = cmake_stage(name, name, "", "", targets=tuple(targets), extra=(
                f"-DGENTEST_CODEGEN_EXECUTABLE={codegen}", f"-DGENTEST_CODEGEN_HOST_CLANG={clangxx}",
                "-DGENTEST_USE_BOOST_JSON=OFF"))
            cross.tools += [arch + "-linux-gnu-g++", "qemu-" + arch]
            cross.dependencies = ["host-codegen"]
        for suite in ("bazel", "meson", "xmake", "recording"):
            work = output / "work" / suite
            prefix = os.environ.get("GENTEST_SERIALIZER_PREFIX", "")
            missing = "GENTEST_SERIALIZER_PREFIX must name installed Glaze and cbor_tags" if suite == "recording" and not prefix else ""
            commands = [] if missing else suite_commands(suite, source, work, jobs=jobs, codegen=codegen,
                serializers=prefix, cc=clang, cxx=clangxx)
            stages.append(Stage(suite, commands, ["cmake", "ninja", clang, clangxx] + ([] if suite == "recording" else [suite]),
                                ["host-codegen"] if suite in ("meson", "xmake") else [], missing))
        # Build both benchmark executables with the same installed toolchain.
        measured = cmake_stage("measured", "release-system", clang, clangxx, origin=base,
            targets=("gentest_benchmarks_tests",), tests=False, extra=("-DGENTEST_ENABLE_PACKAGE_TESTS=OFF",))
        measured.dependencies = ["clang-release"]
        measured.tools.append("bash")
        measured.commands.append(Command(["bash", str(source / "scripts/ci_measured_report_compare.sh")], source, {
            "GENTEST_MEASURED_BASE_EXE": str(output / "work/measured/tests/gentest_benchmarks_tests"),
            "GENTEST_MEASURED_CURRENT_EXE": str(output / "work/clang-release/tests/gentest_benchmarks_tests"),
            "GENTEST_MEASURED_REPORT_DIR": str(output / "evidence/measured")}))
        stages.append(Stage("lint", [Command(["bash", str(source / "scripts/check_clang_format.sh")], source),
            Command(["bash", str(source / "scripts/check_clang_tidy.sh"), str(output / "work/clang-debug")], source)],
            ["bash", "clang-format", "clang-tidy"], ["clang-debug"]))
    else:
        compilers = [("clang", clang, clangxx), ("msvc", "cl", "cl")] if host == "Windows" else [
            ("appleclang", "/usr/bin/clang", "/usr/bin/clang++"), ("llvm", clang, clangxx)]
        for compiler, cc, cxx in compilers:
            for config in ("debug", "release"):
                extra = ("-DGENTEST_SKIP_WINDOWS_DEBUG_DEATH_TESTS=ON",) if host == "Windows" and config == "debug" else ()
                stage = cmake_stage(compiler + "-" + config, config + "-system", cc, cxx, extra=extra)
                if host == "Darwin" and compiler == "llvm" and Path(cxx).resolve() == Path("/usr/bin/clang++").resolve():
                    stage.missing = "Select Homebrew LLVM with GENTEST_LLVM_PREFIX (AppleClang is a separate lane)"
    assert [stage.name for stage in stages] == required_stages(host)
    return stages


def stop_process_tree(process: subprocess.Popen) -> None:
    if os.name == "nt":
        subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    else:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
    process.wait()


def execute(command: Command, log: Path, timeout: int) -> dict:
    start = time.monotonic()
    result = {"command": command.args, "cwd": str(command.cwd), "environment": command.env}
    with log.open("w", encoding="utf-8") as stream:
        try:
            process = subprocess.Popen(command.args, cwd=command.cwd, env=os.environ | command.env,
                stdout=stream, stderr=subprocess.STDOUT, start_new_session=os.name != "nt")
            try:
                result["exit_code"] = process.wait(timeout=timeout)
            except KeyboardInterrupt:
                stop_process_tree(process)
                raise
            except subprocess.TimeoutExpired:
                stop_process_tree(process)
                result.update(exit_code=None, error=f"Command exceeded {timeout}s; process tree terminated")
        except OSError as exc:
            result.update(exit_code=None, error=str(exc))
    result["duration_seconds"] = round(time.monotonic() - start, 3)
    return result


def artifact(path: Path, output: Path) -> dict:
    return {"path": path.relative_to(output).as_posix(), "sha256": sha256(path), "size": path.stat().st_size}


def junit_summary(path: Path) -> dict:
    root = ET.parse(path).getroot()
    cases = list(root.iter("testcase"))
    if not cases:
        raise ValueError(f"Empty JUnit report: {path.name}")
    failed = [case.get("name", "") for case in cases if case.find("failure") is not None or case.find("error") is not None]
    skips = []
    for case in cases:
        skip = case.find("skipped")
        status = case.get("status")
        # CTest writes disabled cases without a <skipped> child. They are not
        # executed and must count toward the all-skipped guard below.
        if skip is None and status not in ("disabled", "notrun"):
            continue
        reason = ((skip.get("message") or skip.text) if skip is not None else None)
        skips.append({"test": case.get("name", ""), "reason": reason or
                      ("CTest disabled test" if status == "disabled" else "CTest marked skipped/not run")})
    return {"tests": len(cases), "failures": failed, "skips": skips}


def run_stage(stage: Stage, output: Path, timeout: int, prior: dict, provenance: dict) -> dict:
    result = {"name": stage.name, "status": "INCOMPLETE", "commands": [], "evidence": [], "tests": [], "known_skips": []}
    missing = [name for name in stage.tools if shutil.which(name) is None]
    blocked = [name for name in stage.dependencies if prior.get(name, {}).get("status") != "PASS"]
    if stage.missing or missing or blocked:
        result["reason"] = stage.missing or (f"Missing tools: {', '.join(missing)}" if missing else f"Prerequisites did not pass: {', '.join(blocked)}")
        return result
    if not stage.commands:
        result["reason"] = "Stage has no commands"
        return result
    if shutil.disk_usage(output).free < 5 * 1024 ** 3:
        result["reason"] = "Less than 5 GiB free; keep this run's evidence and provide capacity for the remaining stages"
        return result
    for name in stage.tools:
        path = Path(shutil.which(name))
        key = str(path)
        if key not in provenance:
            try:
                version = subprocess.run([key, "/?" if path.stem.lower() == "cl" else "--version"],
                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace", timeout=30)
                provenance[key] = {"sha256": sha256(path), "version": version.stdout[:8192], "version_exit": version.returncode}
            except (OSError, subprocess.TimeoutExpired) as exc:
                provenance[key] = {"sha256": sha256(path), "version_error": str(exc)}
    (output / "work" / stage.name).mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    result["status"] = "PASS"
    for index, command in enumerate(stage.commands):
        log = output / "evidence" / f"{stage.name}-{index:02}.log"
        outcome = execute(command, log, timeout)
        outcome["log"] = artifact(log, output)
        result["commands"].append(outcome)
        result["evidence"].append(outcome["log"])
        if outcome["exit_code"] != 0:
            result.update(status="FAIL", reason=outcome.get("error", f"Command {index + 1} exited {outcome['exit_code']}"))
            break
    # Retain JUnit even after CTest fails. A successful CTest command without
    # real test evidence cannot become a successful validation stage.
    for command in result["commands"]:
        args = command["command"]
        if "--output-junit" in args:
            xml = Path(args[args.index("--output-junit") + 1])
            try:
                summary = junit_summary(xml)
                result["tests"].append(summary)
                result["evidence"].append(artifact(xml, output))
                if summary["failures"] or summary["tests"] == len(summary["skips"]):
                    result.update(status="FAIL", reason="JUnit contains failures or no executed tests")
            except (OSError, ValueError, ET.ParseError) as exc:
                result.update(status="FAIL", reason=str(exc))
    for path in (output / "work" / stage.name).rglob("LastTest.log"):
        content = path.read_text(encoding="utf-8", errors="replace")
        result["known_skips"] += re.findall(r"GENTEST_KNOWN_SKIP: ([^\r\n]+)", content)
        result["evidence"].append(artifact(path, output))
    build = output / "work" / stage.name
    for name in ("CMakeCache.txt", "compile_commands.json"):
        for path in build.rglob(name):
            result["evidence"].append(artifact(path, output))
    # Reports used for coverage/performance decisions are evidence too. Retain
    # their hashes alongside the command logs; never copy executable toolchains.
    details = build / "coverage-report" if stage.name == "coverage" else output / "evidence/measured"
    if stage.name in ("coverage", "measured") and details.exists():
        for path in sorted(details.rglob("*")):
            if path.is_file():
                result["evidence"].append(artifact(path, output))
    result["duration_seconds"] = round(time.monotonic() - started, 3)
    return result


def markdown(report: dict) -> str:
    lines = [f"# Private validation: {report['status']}", "", f"Commit: `{report['commit']}`", "",
             "Local evidence only. This file is not intended for publication.", ""]
    if "stages" in report:
        lines += ["| Stage | Result | Detail |", "| --- | --- | --- |"]
        for stage in report["stages"]:
            detail = stage.get("reason", "").replace("|", "\\|").replace("\n", " ")
            lines.append(f"| {stage['name']} | {stage['status']} | {detail} |")
            for skip in stage.get("known_skips", []):
                lines.append(f"| ↳ sub-probe | known skip | {skip} |")
    for error in report.get("errors", []):
        lines.append("- " + error)
    return "\n".join(lines) + "\n"


def save_report(output: Path, report: dict) -> None:
    temporary = output / "report.json.tmp"
    temporary.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    temporary.replace(output / "report.json")
    (output / "summary.md").write_text(markdown(report), encoding="utf-8")


def run(args: argparse.Namespace) -> int:
    host = platform.system()
    required = required_stages(host)
    if args.stage and set(args.stage) - set(required):
        raise ValueError("Unknown stage for this host: " + ", ".join(sorted(set(args.stage) - set(required))))
    commit = verify_source(ROOT, args.commit)
    base_commit = git(ROOT, "rev-parse", "--verify", args.base + "^{commit}")
    output = private_directory(args.output, ROOT)
    (output / "evidence").mkdir()
    report = {"schema": SCHEMA, "commit": commit, "base_commit": base_commit, "host": host,
              "architecture": platform.machine(), "validator": validator_hashes(), "status": "INCOMPLETE",
              "python": {"version": platform.python_version(), "sha256": sha256(Path(sys.executable))},
              "source_verified": False, "stages": [], "tools": {}, "errors": []}
    save_report(output, report)
    source, base = output / "source", output / "base"
    owned: list[Path] = []
    try:
        for path, revision in [(source, commit), (base, base_commit)]:
            git(ROOT, "worktree", "add", "--detach", str(path), revision)
            owned.append(path)
        plan = make_plan(host, source, output, base, args.jobs)
        prior: dict = {}
        for stage in plan:
            print(f"{stage.name}: starting", flush=True)
            if args.stage and stage.name not in args.stage:
                result = {"name": stage.name, "status": "INCOMPLETE", "reason": "Not selected for this partial run"}
            else:
                try:
                    result = run_stage(stage, output, args.timeout, prior, report["tools"])
                except (OSError, ValueError, subprocess.SubprocessError) as exc:
                    result = {"name": stage.name, "status": "FAIL", "reason": str(exc)}
            prior[stage.name] = result
            report["stages"].append(result)
            print(f"{stage.name}: {result['status']}", flush=True)
            save_report(output, report)
        verify_source(source, commit)
        verify_source(base, base_commit)
        report["source_verified"] = True
        statuses = {stage["status"] for stage in report["stages"]}
        report["status"] = "FAIL" if "FAIL" in statuses else "PASS" if statuses == {"PASS"} else "INCOMPLETE"
    except (OSError, ValueError, subprocess.SubprocessError, KeyboardInterrupt) as exc:
        report["errors"].append(str(exc) or "Interrupted")
        report["status"] = "INCOMPLETE"
    finally:
        # Keep owned source/build trees with the private report for diagnosis.
        # They are not a toolchain cache or a copied executable environment.
        report["owned_worktrees"] = [str(path) for path in owned]
        save_report(output, report)
    print(f"{report['status']}: private report in {output}")
    return 0 if report["status"] == "PASS" else 1


def collect(commit: str, directories: list[Path]) -> dict:
    if not SHA.fullmatch(commit):
        raise ValueError("collect needs the full lowercase commit SHA; it never resolves refs or contacts Git")
    combined = {"schema": SCHEMA, "commit": commit, "status": "INCOMPLETE", "hosts": {}, "errors": []}
    bases: set[str] = set()
    for directory in directories:
        directory = directory.resolve()
        try:
            report = json.loads((directory / "report.json").read_text(encoding="utf-8"))
            host = report["host"]
            if host not in HOSTS or host in combined["hosts"]:
                raise ValueError("Unknown or duplicate host report")
            combined["hosts"][host] = {"status": report["status"], "report_sha256": sha256(directory / "report.json")}
            if report.get("schema") != SCHEMA or report.get("validator") != validator_hashes():
                raise ValueError("Report schema or validator version differs")
            if report["commit"] != commit or not report.get("source_verified"):
                raise ValueError("Wrong commit or source was not verified clean")
            if not SHA.fullmatch(report["base_commit"]):
                raise ValueError("Invalid base commit")
            bases.add(report["base_commit"])
            stages = report["stages"]
            if [s["name"] for s in stages] != required_stages(host):
                raise ValueError("Missing, duplicate, or unknown required stages")
            for stage in stages:
                if stage["status"] != "PASS" or not stage.get("commands") or not stage.get("evidence"):
                    raise ValueError(f"{stage['name']} lacks successful complete evidence")
                if any(command.get("exit_code") != 0 for command in stage["commands"]):
                    raise ValueError(f"{stage['name']} contains unsuccessful commands")
                for item in stage["evidence"]:
                    path = (directory / item["path"]).resolve()
                    if not path.is_relative_to(directory) or path == directory:
                        raise ValueError("Evidence path escapes its report directory")
                    if path.stat().st_size != item["size"] or sha256(path) != item["sha256"]:
                        raise ValueError("Evidence hash/size mismatch")
            if report["status"] != "PASS" or report.get("errors"):
                raise ValueError("Host validation did not complete successfully")
        except (OSError, ValueError, KeyError, TypeError) as exc:
            combined["errors"].append(f"{directory.name}: {exc}")
    missing = set(HOSTS) - set(combined["hosts"])
    if missing:
        combined["errors"].append("Missing hosts: " + ", ".join(sorted(missing)))
    if len(bases) != 1:
        combined["errors"].append("Reports must compare against the same base commit")
    if not combined["errors"]:
        combined["status"] = "PASS"
    return combined


def main() -> int:
    if os.name != "nt":
        os.umask(0o077)
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    running = sub.add_parser("run", help="Validate the current host; retain private logs and reports")
    running.add_argument("--commit", required=True)
    running.add_argument("--base", required=True)
    running.add_argument("--output", type=Path, required=True)
    running.add_argument("--jobs", type=int, default=2)
    running.add_argument("--timeout", type=int, default=7200, help="Per-command bound in seconds")
    running.add_argument("--stage", action="append", help="Troubleshooting subset; omitted stages remain INCOMPLETE")
    collecting = sub.add_parser("collect", help="Read already-local reports offline; never publish them")
    collecting.add_argument("--commit", required=True)
    collecting.add_argument("--reports", nargs="+", type=Path, required=True)
    collecting.add_argument("--output", type=Path, help="Optional new private output directory; otherwise print summary")
    args = parser.parse_args()
    try:
        if args.action == "run":
            if args.jobs < 1 or args.timeout < 1:
                raise ValueError("--jobs and --timeout must be positive")
            return run(args)
        report = collect(args.commit, args.reports)
        if args.output:
            save_report(private_directory(args.output, ROOT), report)
        print(markdown(report))
        return 0 if report["status"] == "PASS" else 1
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        print(f"Validation incomplete: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
