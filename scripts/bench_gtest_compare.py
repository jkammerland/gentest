#!/usr/bin/env python3
"""
Compare consumer end-to-end build time for the same test workload in gentest,
GoogleTest, and doctest.

The timed gentest section includes codegen, generated TU compilation, and final
test binary link. It excludes the gentest_codegen tool build and prebuilt
gentest runtime libraries. The timed GoogleTest and doctest sections include
test TU compilation and final test binary link. GoogleTest uses the system
package reported by pkg-config; doctest uses the configured GitHub fork checkout.
The prebuilt gentest_codegen host tool defaults to a Release build even when the
consumer target build type is Debug; override with --codegen-build-type when
you intentionally want to measure a differently built host tool.

Usage:
  python3 scripts/bench_gtest_compare.py
  python3 scripts/bench_gtest_compare.py --samples 5 --jobs 8
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path


DEFAULT_DOCTEST_REPOSITORY = "https://github.com/jkammerland/doctest.git"
DEFAULT_DOCTEST_TAG = "397ce0086a7ba36524c5706dfc2820527260e65b"
DEFAULT_GBENCH_REPOSITORY = "https://github.com/google/benchmark.git"
DEFAULT_GBENCH_TAG = "192ef10025eb2c4cdd392bc502f0c852196baa48"
DEFAULT_NANOBENCH_REPOSITORY = "https://github.com/martinus/nanobench.git"
DEFAULT_NANOBENCH_TAG = "e4327893194f06928012eb81cabc606c4e4791ac"

TEST_TARGETS = ("gentest_compare_tests", "gtest_compare_tests", "doctest_compare_tests")
BENCHMARK_TARGETS = ("gentest_compare_benchmarks", "gbench_compare_benchmarks", "nanobench_compare_benchmarks")
TARGETS = TEST_TARGETS + BENCHMARK_TARGETS


def log(message: str) -> None:
    print(message, flush=True)


def run(cmd: list[str], cwd: Path, env: dict[str, str] | None = None, capture: bool = False) -> subprocess.CompletedProcess[str]:
    kwargs: dict[str, object] = {"cwd": cwd, "env": env, "text": True}
    if capture:
        kwargs.update({"stdout": subprocess.PIPE, "stderr": subprocess.STDOUT})
    return subprocess.run(cmd, check=True, **kwargs)


def repo_root() -> Path:
    out = subprocess.run(
        ["git", "rev-parse", "--show-toplevel"],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    ).stdout.strip()
    return Path(out)


def git(repo: Path, *args: str) -> str:
    return subprocess.run(
        ["git", *args],
        cwd=repo,
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    ).stdout.strip()


def cmake_common_args(build_type: str) -> list[str]:
    return [
        "-G",
        "Ninja",
        f"-DCMAKE_BUILD_TYPE={build_type}",
        "-DCMAKE_TOOLCHAIN_FILE=",
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        "-DCMAKE_CXX_EXTENSIONS=OFF",
    ]


def configure_and_build_codegen(root: Path, build_dir: Path, build_type: str, jobs: int, env: dict[str, str]) -> Path:
    log(f"[bench] Configuring prebuilt gentest_codegen: {build_dir}")
    run(
        [
            "cmake",
            "-S",
            str(root),
            "-B",
            str(build_dir),
            *cmake_common_args(build_type),
            "-Dgentest_BUILD_TESTING=OFF",
            "-DGENTEST_BUILD_CODEGEN=ON",
        ],
        cwd=root,
        env=env,
    )
    log("[bench] Building prebuilt gentest_codegen")
    run(["cmake", "--build", str(build_dir), "--target", "gentest_codegen", "-j", str(jobs)], cwd=root, env=env)

    exe = build_dir / "tools" / ("gentest_codegen.exe" if os.name == "nt" else "gentest_codegen")
    if not exe.exists():
        raise FileNotFoundError(f"gentest_codegen was not produced at {exe}")
    return exe


def configure_harness(
    root: Path,
    build_dir: Path,
    build_type: str,
    codegen_exe: Path,
    doctest_repository: str,
    doctest_tag: str,
    gbench_repository: str,
    gbench_tag: str,
    nanobench_repository: str,
    nanobench_tag: str,
    cmake_args: list[str],
    env: dict[str, str],
) -> None:
    source_dir = root / "benchmarks" / "gtest_compare"
    log(f"[bench] Configuring framework comparison harness: {build_dir}")
    run(
        [
            "cmake",
            "-S",
            str(source_dir),
            "-B",
            str(build_dir),
            *cmake_common_args(build_type),
            f"-DGENTEST_COMPARE_SOURCE_DIR={root}",
            f"-DGENTEST_CODEGEN_EXECUTABLE={codegen_exe}",
            f"-DGENTEST_COMPARE_DOCTEST_REPOSITORY={doctest_repository}",
            f"-DGENTEST_COMPARE_DOCTEST_TAG={doctest_tag}",
            f"-DGENTEST_COMPARE_GBENCH_REPOSITORY={gbench_repository}",
            f"-DGENTEST_COMPARE_GBENCH_TAG={gbench_tag}",
            f"-DGENTEST_COMPARE_NANOBENCH_REPOSITORY={nanobench_repository}",
            f"-DGENTEST_COMPARE_NANOBENCH_TAG={nanobench_tag}",
            *cmake_args,
        ],
        cwd=root,
        env=env,
    )


def executable_path(build_dir: Path, target: str) -> Path:
    suffix = ".exe" if os.name == "nt" else ""
    return build_dir / f"{target}{suffix}"


def clean_consumer_outputs(build_dir: Path, target: str) -> None:
    paths = [
        executable_path(build_dir, target),
        build_dir / "CMakeFiles" / f"{target}.dir",
    ]

    for path in paths:
        if path.is_dir():
            shutil.rmtree(path)
        elif path.exists():
            path.unlink()

    gentest_generated_dirs = {
        "gentest_compare_tests": build_dir / "gentest_compare_generated",
        "gentest_compare_benchmarks": build_dir / "gentest_compare_benchmark_generated",
    }
    generated_dir = gentest_generated_dirs.get(target)
    if generated_dir is not None:
        for pattern in ("*.gentest.h", "*_mock_registry.hpp", "*_mock_impl.hpp", "*.artifact_manifest.json", "*.artifact_manifest.validated"):
            for path in generated_dir.glob(pattern):
                if path.exists():
                    path.unlink()


def classify_outputs(outputs: list[str]) -> str:
    if any(output.endswith(".artifact_manifest.validated") for output in outputs):
        return "gentest_manifest_validation"
    if any(output.endswith(".gentest.h") or output.endswith(".gentest.cpp") for output in outputs):
        return "gentest_codegen_emit"
    if any(output.endswith(".o") and "gentest_compare_tests.dir" in output for output in outputs):
        return "gentest_generated_tu_compile"
    if any(output.endswith(".o") and "gentest_compare_benchmarks.dir" in output for output in outputs):
        return "gentest_generated_benchmark_tu_compile"
    if any(output.endswith(".o") and "gtest_compare_tests.dir" in output for output in outputs):
        return "gtest_tu_compile"
    if any(output.endswith(".o") and "gbench_compare_benchmarks.dir" in output for output in outputs):
        return "gbench_tu_compile"
    if any(output.endswith(".o") and "doctest_compare_tests.dir" in output for output in outputs):
        return "doctest_tu_compile"
    if any(output.endswith(".o") and "nanobench_compare_benchmarks.dir" in output for output in outputs):
        return "nanobench_tu_compile"
    if any(output.endswith("gentest_compare_tests") or output.endswith("gentest_compare_tests.exe") for output in outputs):
        return "gentest_link"
    if any(output.endswith("gentest_compare_benchmarks") or output.endswith("gentest_compare_benchmarks.exe") for output in outputs):
        return "gentest_benchmark_link"
    if any(output.endswith("gtest_compare_tests") or output.endswith("gtest_compare_tests.exe") for output in outputs):
        return "gtest_link"
    if any(output.endswith("gbench_compare_benchmarks") or output.endswith("gbench_compare_benchmarks.exe") for output in outputs):
        return "gbench_link"
    if any(output.endswith("doctest_compare_tests") or output.endswith("doctest_compare_tests.exe") for output in outputs):
        return "doctest_link"
    if any(output.endswith("nanobench_compare_benchmarks") or output.endswith("nanobench_compare_benchmarks.exe") for output in outputs):
        return "nanobench_link"
    if any("CTestTestfile" in output for output in outputs):
        return "ctest_metadata"
    return "other"


def ninja_log_line_count(ninja_log: Path) -> int:
    if not ninja_log.exists():
        return 0
    with ninja_log.open("r", encoding="utf-8") as f:
        return sum(1 for _ in f)


def summarize_ninja_log(build_dir: Path, skip_lines: int = 0) -> dict[str, object]:
    ninja_log = build_dir / ".ninja_log"
    summary: dict[str, object] = {
        "output_entries": 0,
        "unique_edges": 0,
        "wall_s": 0.0,
        "categories": {},
        "top_edges": [],
    }
    if not ninja_log.exists():
        return summary

    edges: dict[tuple[int, int, str], list[str]] = {}
    for line in ninja_log.read_text(encoding="utf-8").splitlines()[skip_lines:]:
        if not line or line.startswith("#"):
            continue
        fields = line.split("\t")
        if len(fields) < 5:
            continue
        try:
            start_ms = int(fields[0])
            end_ms = int(fields[1])
        except ValueError:
            continue
        edges.setdefault((start_ms, end_ms, fields[4]), []).append(fields[3])

    categories: dict[str, dict[str, float | int]] = {}
    top_edges: list[dict[str, object]] = []
    output_entries = 0
    wall_ms = 0
    for key, outputs in edges.items():
        start_ms, end_ms, _ = key
        wall_ms = max(wall_ms, end_ms)
        output_entries += len(outputs)
        duration_s = (end_ms - start_ms) / 1000.0
        category = classify_outputs(outputs)

        item = categories.setdefault(category, {"count": 0, "summed_edge_s": 0.0, "max_edge_s": 0.0})
        item["count"] = int(item["count"]) + 1
        item["summed_edge_s"] = float(item["summed_edge_s"]) + duration_s
        item["max_edge_s"] = max(float(item["max_edge_s"]), duration_s)
        top_edges.append(
            {
                "category": category,
                "duration_s": duration_s,
                "start_s": start_ms / 1000.0,
                "end_s": end_ms / 1000.0,
                "output": outputs[0],
            }
        )

    summary["output_entries"] = output_entries
    summary["unique_edges"] = len(edges)
    summary["wall_s"] = wall_ms / 1000.0
    summary["categories"] = categories
    summary["top_edges"] = sorted(top_edges, key=lambda edge: float(edge["duration_s"]), reverse=True)[:20]
    return summary


def time_target(root: Path, build_dir: Path, target: str, jobs: int, env: dict[str, str], sample: int) -> dict[str, object]:
    clean_consumer_outputs(build_dir, target)
    ninja_log = build_dir / ".ninja_log"
    ninja_log_start_lines = ninja_log_line_count(ninja_log)

    log(f"[bench] Starting {target} sample {sample} (-j{jobs})")
    cmd = ["cmake", "--build", str(build_dir), "--target", target, "-j", str(jobs)]
    start = time.perf_counter()
    proc = subprocess.run(cmd, cwd=root, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    elapsed = time.perf_counter() - start
    if proc.returncode != 0:
        print(proc.stdout[-6000:], file=sys.stderr)
        raise subprocess.CalledProcessError(proc.returncode, cmd, output=proc.stdout)

    log(f"[bench] Finished {target} sample {sample}: {elapsed:.3f}s")
    return {"elapsed_s": elapsed, "ninja_log": summarize_ninja_log(build_dir, ninja_log_start_lines)}


def stats(samples: list[float]) -> dict[str, object]:
    return {
        "samples_s": samples,
        "median_s": statistics.median(samples),
        "mean_s": statistics.mean(samples),
        "min_s": min(samples),
        "max_s": max(samples),
    }


def pkg_config_version(module: str) -> str:
    try:
        return subprocess.run(
            ["pkg-config", "--modversion", module],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        ).stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return "unavailable"


def run_command_for_target(build_dir: Path, target: str) -> list[str]:
    command = [str(executable_path(build_dir, target))]
    if target == "gentest_compare_benchmarks":
        command.extend(
            [
                "--kind=bench",
                "--bench-epochs=1",
                "--bench-warmup=0",
                "--bench-min-epoch-time-s=0",
                "--bench-min-total-time-s=0",
                "--bench-max-total-time-s=10",
            ]
        )
    return command


def print_profile(title: str, profile: dict[str, object]) -> None:
    print(title)
    print(f"Wall from Ninja log: {float(profile.get('wall_s', 0.0)):.3f}s")
    categories = profile.get("categories", {})
    if not isinstance(categories, dict) or not categories:
        print("  no ninja profile data")
        return
    print(f"{'Category':<32} | {'Edges':>5} | {'Sum edge-s':>10} | {'Max edge-s':>10}")
    print("-" * 68)
    for name, data in sorted(categories.items(), key=lambda item: float(item[1].get("summed_edge_s", 0.0)), reverse=True):
        if not isinstance(data, dict):
            continue
        print(
            f"{name:<32} | {int(data.get('count', 0)):>5} | "
            f"{float(data.get('summed_edge_s', 0.0)):>10.3f} | {float(data.get('max_edge_s', 0.0)):>10.3f}"
        )


def print_summary(result: dict[str, object]) -> None:
    gentest = result["gentest"]
    gtest = result["gtest"]
    doctest = result["doctest"]
    deltas = result["deltas"]
    benchmark_results = result["benchmarks"]
    benchmark_deltas = result["benchmark_deltas"]
    assert isinstance(gentest, dict)
    assert isinstance(gtest, dict)
    assert isinstance(doctest, dict)
    assert isinstance(deltas, dict)
    assert isinstance(benchmark_results, dict)
    assert isinstance(benchmark_deltas, dict)

    print("\n=== Test Framework Consumer E2E Build Benchmark ===")
    print(f"Commit:       {result['sha']}")
    print(f"Build type:   {result['build_type']}")
    print(f"Codegen type: {result['codegen_build_type']}")
    print(f"Jobs:         {result['jobs']}")
    print(f"GoogleTest:   {result['gtest_version']} (pkg-config)")
    print(f"gbench:       {result['gbench_repository']} @ {result['gbench_tag']}")
    print(f"doctest:      {result['doctest_repository']} @ {result['doctest_tag']}")
    print(f"nanobench:    {result['nanobench_repository']} @ {result['nanobench_tag']}")
    print(f"Test workload: unit + integration + skiponly ports; 26 tests, 3 skipped")
    print(f"Bench workload: append iota into vector/list/deque with 1,000,000 elements")
    print("Timed scope:  consumer test/benchmark binary builds only")
    print("Excluded:     gentest_codegen build, gentest runtime build, Google Benchmark build, GoogleTest build, doctest/nanobench helper builds")
    print()
    print("Test Binary Compile")
    print(f"{'Framework':<12} | {'Median (s)':>10} | {'Mean (s)':>10} | Samples")
    print("-" * 76)
    print(f"{'gentest':<12} | {gentest['median_s']:>10.3f} | {gentest['mean_s']:>10.3f} | {', '.join(f'{v:.3f}' for v in gentest['samples_s'])}")
    print(f"{'googletest':<12} | {gtest['median_s']:>10.3f} | {gtest['mean_s']:>10.3f} | {', '.join(f'{v:.3f}' for v in gtest['samples_s'])}")
    print(f"{'doctest':<12} | {doctest['median_s']:>10.3f} | {doctest['mean_s']:>10.3f} | {', '.join(f'{v:.3f}' for v in doctest['samples_s'])}")
    print()
    for label, delta in deltas.items():
        if not isinstance(delta, dict):
            continue
        print(f"Delta (gentest - {label}): {delta['median_s']:+.3f}s ({delta['median_pct']:+.2f}%)")

    print()
    print("Benchmark Binary Compile")
    print(f"{'Framework':<12} | {'Median (s)':>10} | {'Mean (s)':>10} | Samples")
    print("-" * 76)
    for label in ("gentest", "gbench", "nanobench"):
        data = benchmark_results[label]
        assert isinstance(data, dict)
        print(f"{label:<12} | {data['median_s']:>10.3f} | {data['mean_s']:>10.3f} | {', '.join(f'{v:.3f}' for v in data['samples_s'])}")
    print()
    for label, delta in benchmark_deltas.items():
        if not isinstance(delta, dict):
            continue
        print(f"Delta (gentest bench - {label}): {delta['median_s']:+.3f}s ({delta['median_pct']:+.2f}%)")

    for framework_label, data in (("Gentest", gentest), ("GoogleTest", gtest), ("doctest", doctest)):
        profiles = data.get("profiles", [])
        if isinstance(profiles, list) and profiles:
            print()
            print_profile(f"{framework_label} Last-Sample Profile", profiles[-1])

    for framework_label, data in (
        ("Gentest Bench", benchmark_results["gentest"]),
        ("Google Benchmark", benchmark_results["gbench"]),
        ("nanobench", benchmark_results["nanobench"]),
    ):
        assert isinstance(data, dict)
        profiles = data.get("profiles", [])
        if isinstance(profiles, list) and profiles:
            print()
            print_profile(f"{framework_label} Last-Sample Profile", profiles[-1])


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--samples", type=int, default=3, help="Number of paired clean consumer build samples")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 1, help="Parallel build jobs")
    ap.add_argument("--build-type", default="Release", help="CMake build type")
    ap.add_argument(
        "--codegen-build-type",
        default="Release",
        help="CMake build type for the prebuilt gentest_codegen host tool",
    )
    ap.add_argument("--output", default=None, help="JSON output path")
    ap.add_argument("--cmake-arg", action="append", default=[], help="Extra CMake argument for the harness configure")
    ap.add_argument("--ccache", choices=["off", "on"], default="off", help="Disable ccache by default for stable timings")
    ap.add_argument("--skip-run", action="store_true", help="Do not run the comparison binaries after the initial build")
    ap.add_argument("--doctest-repository", default=DEFAULT_DOCTEST_REPOSITORY, help="doctest Git repository used by the harness")
    ap.add_argument("--doctest-tag", default=DEFAULT_DOCTEST_TAG, help="doctest commit, tag, or branch used by the harness")
    ap.add_argument("--gbench-repository", default=DEFAULT_GBENCH_REPOSITORY, help="Google Benchmark Git repository used by the harness")
    ap.add_argument("--gbench-tag", default=DEFAULT_GBENCH_TAG, help="Google Benchmark commit, tag, or branch used by the harness")
    ap.add_argument("--nanobench-repository", default=DEFAULT_NANOBENCH_REPOSITORY, help="nanobench Git repository used by the harness")
    ap.add_argument("--nanobench-tag", default=DEFAULT_NANOBENCH_TAG, help="nanobench commit, tag, or branch used by the harness")
    args = ap.parse_args()

    if args.samples < 1:
        print("error: --samples must be at least 1", file=sys.stderr)
        return 2
    if args.jobs < 1:
        print("error: --jobs must be at least 1", file=sys.stderr)
        return 2

    root = repo_root()
    env = os.environ.copy()
    if args.ccache == "off":
        env["CCACHE_DISABLE"] = "1"

    output = Path(args.output) if args.output else root / "build" / "bench-gtest-compare" / "gtest_compare.json"
    if not output.is_absolute():
        output = root / output
    output.parent.mkdir(parents=True, exist_ok=True)

    build_root = root / "build" / "bench-gtest-compare"
    tool_build = build_root / "tool"
    harness_build = build_root / "harness"

    sha = git(root, "rev-parse", "HEAD")
    codegen_exe = configure_and_build_codegen(root, tool_build, args.codegen_build_type, args.jobs, env)
    configure_harness(
        root,
        harness_build,
        args.build_type,
        codegen_exe,
        args.doctest_repository,
        args.doctest_tag,
        args.gbench_repository,
        args.gbench_tag,
        args.nanobench_repository,
        args.nanobench_tag,
        args.cmake_arg,
        env,
    )

    log("[bench] Prebuilding framework/runtime dependencies")
    run(["cmake", "--build", str(harness_build), "--target", "gentest_main", "-j", str(args.jobs)], cwd=root, env=env)
    run(["cmake", "--build", str(harness_build), "--target", "doctest_compare_main", "-j", str(args.jobs)], cwd=root, env=env)
    run(["cmake", "--build", str(harness_build), "--target", "benchmark_main", "-j", str(args.jobs)], cwd=root, env=env)
    run(["cmake", "--build", str(harness_build), "--target", "nanobench", "-j", str(args.jobs)], cwd=root, env=env)

    log("[bench] Building and validating comparison binaries once")
    for target in TARGETS:
        run(["cmake", "--build", str(harness_build), "--target", target, "-j", str(args.jobs)], cwd=root, env=env)
    if not args.skip_run:
        for target in TARGETS:
            run(run_command_for_target(harness_build, target), cwd=harness_build, env=env, capture=True)

    samples: dict[str, list[float]] = {target: [] for target in TARGETS}
    profiles: dict[str, list[dict[str, object]]] = {target: [] for target in TARGETS}
    for sample in range(1, args.samples + 1):
        for target in TARGETS:
            result = time_target(root, harness_build, target, args.jobs, env, sample)
            samples[target].append(float(result["elapsed_s"]))
            profile = result["ninja_log"]
            assert isinstance(profile, dict)
            profiles[target].append(profile)

    gentest_stats = {**stats(samples["gentest_compare_tests"]), "profiles": profiles["gentest_compare_tests"]}
    gtest_stats = {**stats(samples["gtest_compare_tests"]), "profiles": profiles["gtest_compare_tests"]}
    doctest_stats = {**stats(samples["doctest_compare_tests"]), "profiles": profiles["doctest_compare_tests"]}
    gentest_bench_stats = {**stats(samples["gentest_compare_benchmarks"]), "profiles": profiles["gentest_compare_benchmarks"]}
    gbench_stats = {**stats(samples["gbench_compare_benchmarks"]), "profiles": profiles["gbench_compare_benchmarks"]}
    nanobench_stats = {**stats(samples["nanobench_compare_benchmarks"]), "profiles": profiles["nanobench_compare_benchmarks"]}

    def delta_against(base: dict[str, object], other: dict[str, object]) -> dict[str, float]:
        median_delta = float(base["median_s"]) - float(other["median_s"])
        median_pct = (median_delta / float(other["median_s"]) * 100.0) if float(other["median_s"]) else 0.0
        return {"median_s": median_delta, "median_pct": median_pct}

    deltas = {
        "googletest": delta_against(gentest_stats, gtest_stats),
        "doctest": delta_against(gentest_stats, doctest_stats),
    }
    benchmark_results = {
        "gentest": gentest_bench_stats,
        "gbench": gbench_stats,
        "nanobench": nanobench_stats,
    }
    benchmark_deltas = {
        "gbench": delta_against(gentest_bench_stats, gbench_stats),
        "nanobench": delta_against(gentest_bench_stats, nanobench_stats),
    }

    result: dict[str, object] = {
        "sha": sha,
        "build_type": args.build_type,
        "codegen_build_type": args.codegen_build_type,
        "jobs": args.jobs,
        "samples": args.samples,
        "ccache": args.ccache,
        "gtest_version": pkg_config_version("gtest"),
        "doctest_repository": args.doctest_repository,
        "doctest_tag": args.doctest_tag,
        "gbench_repository": args.gbench_repository,
        "gbench_tag": args.gbench_tag,
        "nanobench_repository": args.nanobench_repository,
        "nanobench_tag": args.nanobench_tag,
        "timed_scope": "consumer test/benchmark binary builds",
        "excluded": [
            "gentest_codegen build",
            "gentest runtime build",
            "Google Benchmark build",
            "installed GoogleTest library build",
            "doctest main/helper build",
            "nanobench build",
        ],
        "workload": {
            "source_suites": ["tests/unit/cases.cpp", "tests/integration/cases.cpp", "tests/skiponly/cases.cpp"],
            "tests": 26,
            "skipped": 3,
            "benchmarks": ["append_iota/vector_1m", "append_iota/list_1m", "append_iota/deque_1m"],
            "benchmark_elements": 1_000_000,
        },
        "gentest": gentest_stats,
        "gtest": gtest_stats,
        "doctest": doctest_stats,
        "benchmarks": benchmark_results,
        "benchmark_deltas": benchmark_deltas,
        "deltas": deltas,
        "delta": deltas["googletest"],
    }

    output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    log(f"[bench] Wrote {output}")
    print_summary(result)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
