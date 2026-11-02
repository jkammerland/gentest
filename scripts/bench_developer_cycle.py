#!/usr/bin/env python3
"""Measure source adoption and a fixed, verified C++ edit/build/test sequence."""

from __future__ import annotations

import argparse
from collections import Counter
import datetime
import json
import os
import platform
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path


WORKLOAD = Path(__file__).resolve().parent / "bench_workloads" / "developer_cycle"
SCENARIOS = ("cold", "no_op", "body_edit", "added_case", "added_row", "production_header_edit")


def output(command: list[str], cwd: Path) -> str:
    return subprocess.check_output(command, cwd=cwd, text=True, stderr=subprocess.STDOUT).strip()


def replace_once(path: Path, before: str, after: str) -> None:
    contents = path.read_text(encoding="utf-8")
    if contents.count(before) != 1:
        raise ValueError(f"expected exactly one edit anchor in {path}: {before!r}")
    path.write_text(contents.replace(before, after, 1), encoding="utf-8")


def apply_edit(source: Path, scenario: str, expected: set[str]) -> None:
    # Edits accumulate in the documented order; each sample starts from a fresh copy.
    if scenario == "body_edit":
        replace_once(source / "cases.cpp", "const auto input = 6 + 8;", "const auto input = 5 + 9;")
    elif scenario == "added_case":
        replace_once(source / "cases.hpp", "// Added cases belong here.", '[[gentest::test("added")]]\nvoid added();')
        replace_once(
            source / "cases.cpp",
            "// Added cases belong here.",
            "void added() {\n    gentest::expect_eq(app::normalizedCents(50), 50);\n}",
        )
        expected.add("cycle/added")
    elif scenario == "added_row":
        replace_once(source / "cases.hpp", "(101, 100))]]", "(101, 100), (50, 50))]]")
        expected.add("cycle/rows(50, 50)")
    elif scenario == "production_header_edit":
        replace_once(
            source / "production.hpp",
            "return std::clamp(value, 0, 100);",
            "return std::min(std::max(value, 0), 100);",
        )


def ninja_actions(lines: list[str]) -> list[dict]:
    # One Ninja edge can publish several outputs. Count it once, retaining all outputs.
    edges: dict[tuple[int, int, str], dict] = {}
    for line in lines:
        if not line or line.startswith("#"):
            continue
        start, end, _mtime, name, command_hash = line.split("\t")
        key = (int(start), int(end), command_hash)
        edge = edges.setdefault(key, {"start_ms": key[0], "end_ms": key[1], "outputs": []})
        edge["outputs"].append(name)
    return list(edges.values())


def new_ninja_lines(before: list[str], after: list[str]) -> list[str]:
    # CMake regeneration can restart Ninja and compact/reorder earlier log records.
    # Subtract prior records rather than treating the log as append-only.
    previous = Counter(before)
    added = []
    for line in after:
        if previous[line]:
            previous[line] -= 1
        else:
            added.append(line)
    return added


def action_category(outputs: list[str]) -> str:
    names = " ".join(outputs).replace("\\", "/")

    # CMake also emits files inside component source directories during regeneration.
    if any(name.replace("\\", "/").rsplit("/", 1)[-1] == "build.ninja" for name in outputs):
        return "cmake_regeneration"
    if "_deps/" in names:
        return "dependency_construction"
    if "gentest_codegen.dir/" in names or any(name.endswith("/gentest_codegen") for name in outputs):
        return "host_codegen_construction"
    if "gentest/src/" in names:
        return "runtime_construction"
    if any(name.endswith(".header_registration.gentest.cpp") for name in outputs):
        return "mock_generation" if "generated/mocks/" in names else "registration_generation"
    if any(name.endswith(("_mock_registry.hpp", "_mock_impl.hpp")) for name in outputs):
        return "mock_generation"
    if "header_registration.gentest.cpp.o" in names:
        return "registration_compilation"
    if any(name.endswith(".o") for name in outputs):
        return "authored_and_mock_compilation"
    if any(name == "cycle_tests" for name in outputs):
        return "consumer_link"
    return "other"


def action_summary(actions: list[dict]) -> dict:
    summary: dict[str, dict] = {}
    for action in actions:
        category = action_category(action["outputs"])
        entry = summary.setdefault(category, {"actions": 0, "action_seconds": 0.0})
        entry["actions"] += 1
        entry["action_seconds"] += (action["end_ms"] - action["start_ms"]) / 1000
    return summary


def timed(command: list[str], cwd: Path, env: dict[str, str], log: Path, record: dict, phase: str) -> str:
    print(f"[{record['sample']}:{record['scenario']}] {phase}", flush=True)
    start = time.perf_counter()
    with log.open("w", encoding="utf-8") as stream:
        result = subprocess.run(command, cwd=cwd, env=env, text=True, stdout=stream, stderr=subprocess.STDOUT)
    elapsed = time.perf_counter() - start
    record["phases"].append({"phase": phase, "command": command, "seconds": elapsed, "returncode": result.returncode, "log": str(log)})
    contents = log.read_text(encoding="utf-8")
    if result.returncode != 0:
        raise RuntimeError(f"{phase} exited {result.returncode}; see {log}\n{contents[-6000:]}")
    return contents


def validate_inventory(names: list[str], ctest_names: list[str], expected: set[str]) -> None:
    if not expected or len(names) != len(expected) or set(names) != expected:
        raise ValueError(f"runner inventory mismatch: expected {sorted(expected)}, got {names}")
    if len(ctest_names) != len(expected) or set(ctest_names) != expected:
        raise ValueError(f"CTest inventory mismatch: expected {sorted(expected)}, got {ctest_names}")


def read_ninja_log(build: Path) -> list[str]:
    path = build / ".ninja_log"
    return path.read_text(encoding="utf-8").splitlines() if path.exists() else []


def run_sample(args: argparse.Namespace, sample: int, results: dict, env: dict[str, str]) -> None:
    root = args.work_dir / f"sample-{sample:02d}"
    source, build, logs = root / "consumer", root / "build", root / "logs"
    shutil.copytree(WORKLOAD, source)
    logs.mkdir()
    expected = set((source / "expected_tests.txt").read_text(encoding="utf-8").splitlines())
    if len(expected) != 9:
        raise ValueError("the initial workload must contain exactly nine distinct cases")

    for scenario in SCENARIOS:
        apply_edit(source, scenario, expected)
        record = {
            "sample": sample, "scenario": scenario, "phases": [], "expected_cases": sorted(expected),
            "started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
            "load_average_before": os.getloadavg() if hasattr(os, "getloadavg") else None,
        }
        results["runs"].append(record)

        def run(phase: str, command: list[str]) -> str:
            return timed(command, root, env, logs / f"{scenario}-{phase}.log", record, phase)

        def build_target(phase: str, target: str) -> None:
            before = read_ninja_log(build)
            run(phase, ["cmake", "--build", str(build), "--parallel", str(args.jobs), "--target", target])
            after = read_ninja_log(build)
            actions = ninja_actions(new_ninja_lines(before, after))
            record.setdefault("build_actions", []).extend(actions)

        if scenario == "cold":
            run("configure_and_dependencies", [
                "cmake", "-S", str(source), "-B", str(build), "-G", "Ninja", *args.cmake_arg,
                f"-DGENTEST_SOURCE_DIR={args.source}", f"-DCMAKE_BUILD_TYPE={args.build_type}",
                f"-DCMAKE_C_COMPILER={args.cc}", f"-DCMAKE_CXX_COMPILER={args.cxx}",
                "-DCMAKE_C_COMPILER_LAUNCHER=", "-DCMAKE_CXX_COMPILER_LAUNCHER=", "-DCMAKE_TOOLCHAIN_FILE=",
                f"-DGENTEST_CODEGEN_JOBS={args.codegen_jobs}",
            ])
            build_target("host_codegen_build", "gentest_codegen")
            build_target("runtime_build", "gentest_main")
            record["cmake_cache"] = (build / "CMakeCache.txt").read_text(encoding="utf-8").splitlines()
            record["dependencies"] = (build / "benchmark_dependencies.txt").read_text(encoding="utf-8").splitlines()

        build_target("consumer_build", "cycle_tests")
        if scenario == "no_op" and record.get("build_actions"):
            raise ValueError("no-op rebuild unexpectedly executed Ninja actions")
        if scenario not in ("cold", "no_op") and not record.get("build_actions"):
            raise ValueError(f"{scenario} did not execute any rebuild actions")
        record["action_summary"] = action_summary(record.get("build_actions", []))

        # Validate non-vacuous inventories before trusting any passing test command.
        executable = build / "cycle_tests"
        names = run("inventory", [str(executable), "--list-tests", "--no-color"]).splitlines()
        ctest = json.loads(run("ctest_inventory", ["ctest", "--test-dir", str(build), "--show-only=json-v1"]))
        validate_inventory(names, [test["name"] for test in ctest["tests"]], expected)
        run("ctest", ["ctest", "--test-dir", str(build), "--output-on-failure", "--parallel", "1"])
        run("shared_fixture_validation", [str(executable), "--no-color"])

        record["developer_loop_seconds"] = sum(phase["seconds"] for phase in record["phases"] if phase["phase"] in ("consumer_build", "ctest"))
        record["adoption_seconds"] = sum(phase["seconds"] for phase in record["phases"] if phase["phase"] not in ("inventory", "ctest_inventory", "shared_fixture_validation"))
        record["validated"] = True
        record["load_average_after"] = os.getloadavg() if hasattr(os, "getloadavg") else None
        print(f"  build + CTest: {record['developer_loop_seconds']:.3f}s; {len(expected)} cases verified", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--work-dir", type=Path, required=True, help="new directory; existing directories are rejected")
    parser.add_argument("--samples", type=int, default=3)
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--codegen-jobs", type=int, default=1)
    parser.add_argument("--build-type", default="Release", choices=("Debug", "Release", "RelWithDebInfo"))
    parser.add_argument("--cc", default="clang")
    parser.add_argument("--cxx", default="clang++")
    parser.add_argument("--cmake-arg", action="append", default=[])
    parser.add_argument("--allow-dirty", action="store_true")
    parser.add_argument("--machine-note", required=True, help="background load, power/CPU policy, and any other relevant machine conditions")
    args = parser.parse_args()
    if args.samples < 1 or args.jobs < 1 or args.codegen_jobs < 0:
        parser.error("samples/jobs must be positive; codegen-jobs must be nonnegative")
    args.source, args.work_dir = args.source.resolve(), args.work_dir.resolve()
    for compiler in ("cc", "cxx"):
        resolved = shutil.which(getattr(args, compiler))
        if resolved is None:
            parser.error(f"compiler not found: {getattr(args, compiler)}")
        setattr(args, compiler, resolved)
    status = output(["git", "status", "--porcelain"], args.source)
    if status and not args.allow_dirty:
        parser.error("source checkout is dirty; commit it or pass --allow-dirty to record that limitation")
    if args.work_dir.exists():
        parser.error("work-dir already exists; choose a new directory")
    args.work_dir.mkdir(parents=True)

    env = dict(os.environ, CCACHE_DISABLE="1")
    versions = {name: output([name, "--version"], args.source) for name in ("cmake", "ninja", args.cc, args.cxx)}
    cpuinfo = Path("/proc/cpuinfo")
    cpu = next((line for line in cpuinfo.read_text().splitlines() if line.startswith("model name")), "unavailable") if cpuinfo.exists() else platform.processor()
    cpu_limit = Path("/sys/fs/cgroup/cpu.max")
    results = {
        "schema_version": 1, "started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "revision": output(["git", "rev-parse", "HEAD"], args.source), "source_status": status,
        "harness_revision": output(["git", "rev-parse", "HEAD"], WORKLOAD),
        "harness_status": output(["git", "status", "--porcelain"], WORKLOAD),
        "command": sys.argv, "source": str(args.source), "versions": versions,
        "machine": dict(
            platform.uname()._asdict(), cpu=cpu, logical_cpus=os.cpu_count(), note=args.machine_note,
            visible_cgroup_cpu_max=cpu_limit.read_text().strip() if cpu_limit.exists() else None,
        ),
        "settings": {key: value for key, value in vars(args).items() if key not in ("source", "work_dir")},
        "cache_policy": "fresh per-sample consumer/build/_deps; compiler launchers disabled; CCACHE_DISABLE=1; system dependencies and OS page cache retained",
        "build_environment": {key: env.get(key) for key in ("CFLAGS", "CXXFLAGS", "CPPFLAGS", "LDFLAGS", "CMAKE_PREFIX_PATH", "LLVM_DIR", "Clang_DIR")},
        "runs": [],
    }
    try:
        for sample in range(1, args.samples + 1):
            run_sample(args, sample, results, env)
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        results["error"] = str(error)
        print(f"error: {error}", file=sys.stderr)
    finally:
        (args.work_dir / "results.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    if "error" in results:
        return 1
    print("\nScenario                     median build + CTest (s)   range (s)")
    for scenario in SCENARIOS:
        samples = [run["developer_loop_seconds"] for run in results["runs"] if run["scenario"] == scenario]
        print(f"{scenario:28s} {statistics.median(samples):9.3f}                {min(samples):.3f}..{max(samples):.3f}")
    cold = [run["adoption_seconds"] for run in results["runs"] if run["scenario"] == "cold"]
    print(f"Cold source adoption median: {statistics.median(cold):.3f}s (configure/dependencies + framework/consumer builds + CTest)")
    print(f"Results and command logs: {args.work_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
