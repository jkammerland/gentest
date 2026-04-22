#!/usr/bin/env python3
"""
Compare Release end-to-end test binary build time against a base ref.

This benchmark intentionally excludes the time needed to compile the
gentest_codegen tool itself:

  1. Build a Release gentest_codegen once for the current checkout.
  2. Build a Release gentest_codegen once for the base checkout.
  3. Configure separate Release test builds with GENTEST_BUILD_CODEGEN=OFF and
     GENTEST_CODEGEN_EXECUTABLE pointing at the matching prebuilt tool.
  4. Time clean builds of representative generated test binaries.

The timed section includes code generation, generated/mock artifacts, runtime
library compilation, test binary compilation, and linking. It does not run the
test binaries.

Usage examples:
  python3 scripts/bench_release_e2e_compare.py
  python3 scripts/bench_release_e2e_compare.py --base-ref master
  python3 scripts/bench_release_e2e_compare.py --base-ref 44e5273c --samples 5
  python3 scripts/bench_release_e2e_compare.py --targets gentest_unit_tests,gentest_mocking_tests
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path


DEFAULT_TARGETS = [
    "gentest_unit_tests",
    "gentest_integration_tests",
    "gentest_fixtures_tests",
    "gentest_readme_fixtures_tests",
    "gentest_templates_tests",
    "gentest_mocking_tests",
    "gentest_ctor_tests",
    "gentest_benchmarks_tests",
    "gentest_outcomes_tests",
    "gentest_concurrency_tests",
    "gentest_skiponly_tests",
    "gentest_failing_tests",
]


def run(cmd, cwd, env=None, capture=False):
    kwargs = {"cwd": cwd, "env": env, "text": True}
    if capture:
        kwargs.update({"stdout": subprocess.PIPE, "stderr": subprocess.STDOUT})
    return subprocess.run(cmd, check=True, **kwargs)


def log(message: str) -> None:
    print(message, flush=True)


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


def configure_and_build_tool(source_dir: Path, build_dir: Path, build_type: str, jobs: int, env: dict[str, str]) -> Path:
    log(f"[bench] Configuring prebuilt gentest_codegen: {source_dir}")
    cmd = [
        "cmake",
        "-S",
        str(source_dir),
        "-B",
        str(build_dir),
        *cmake_common_args(build_type),
        "-Dgentest_BUILD_TESTING=OFF",
        "-DGENTEST_BUILD_CODEGEN=ON",
    ]
    run(cmd, cwd=source_dir, env=env)

    log(f"[bench] Building prebuilt gentest_codegen: {build_dir}")
    run(["cmake", "--build", str(build_dir), "--target", "gentest_codegen", "-j", str(jobs)], cwd=source_dir, env=env)

    exe = build_dir / "tools" / ("gentest_codegen.exe" if os.name == "nt" else "gentest_codegen")
    if not exe.exists():
        raise FileNotFoundError(f"gentest_codegen was not produced at {exe}")
    return exe


def configure_timed_build(
    source_dir: Path,
    build_dir: Path,
    build_type: str,
    codegen_exe: Path,
    cmake_args: list[str],
    env: dict[str, str],
) -> None:
    log(f"[bench] Configuring timed build: {source_dir}")
    cmd = [
        "cmake",
        "-S",
        str(source_dir),
        "-B",
        str(build_dir),
        *cmake_common_args(build_type),
        "-Dgentest_BUILD_TESTING=ON",
        "-DGENTEST_ENABLE_PACKAGE_TESTS=OFF",
        "-DGENTEST_BUILD_CODEGEN=OFF",
        f"-DGENTEST_CODEGEN_EXECUTABLE={codegen_exe}",
        *cmake_args,
    ]
    run(cmd, cwd=source_dir, env=env)


def clean_build_sample(
    label: str,
    source_dir: Path,
    build_dir: Path,
    targets: list[str],
    jobs: int,
    env: dict[str, str],
    sample: int,
) -> float:
    log(f"[bench] Starting {label} sample {sample} (-j{jobs})")
    run(["cmake", "--build", str(build_dir), "--target", "clean", "-j", str(jobs)], cwd=source_dir, env=env)

    ninja_log = build_dir / ".ninja_log"
    if ninja_log.exists():
        ninja_log.unlink()

    cmd = ["cmake", "--build", str(build_dir), "--target", *targets, "-j", str(jobs)]
    start = time.perf_counter()
    proc = subprocess.run(cmd, cwd=source_dir, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    elapsed = time.perf_counter() - start
    if proc.returncode != 0:
        print(proc.stdout[-6000:], file=sys.stderr)
        raise subprocess.CalledProcessError(proc.returncode, cmd, output=proc.stdout)
    log(f"[bench] Finished {label} sample {sample}: {elapsed:.3f}s")
    return elapsed


def classify_ninja_outputs(outputs: list[str]) -> str:
    if (
        any(output.endswith(".artifact_manifest.validated") for output in outputs)
    ):
        return "manifest_validation_json"
    if any(
        output.endswith(".gentest.h")
        or output.endswith(".gentest.cpp")
        or output.endswith("_mock_registry.hpp")
        or output.endswith("_mock_impl.hpp")
        or "_mock_registry__domain_" in output
        or "_mock_impl__domain_" in output
        or output.endswith(".artifact_manifest.json")
        for output in outputs
    ):
        return "codegen_execution_emit"
    if any(output.startswith("src/CMakeFiles/") and output.endswith(".o") for output in outputs):
        return "runtime_compile"
    if any(output.startswith("src/libgentest") for output in outputs):
        return "runtime_archive"
    if any("/tu_" in output and output.endswith(".gentest.cpp.o") for output in outputs):
        return "generated_test_tu_compile"
    if any(output.startswith("tests/CMakeFiles/") and output.endswith(".o") for output in outputs):
        return "test_support_compile"
    if any(output.startswith("tests/lib") and output.endswith(".a") for output in outputs):
        return "test_helper_archive"
    if any(output.startswith("tests/gentest_") and not output.endswith(".a") for output in outputs):
        return "final_test_binary_link"
    if any("CTestTestfile" in output for output in outputs):
        return "test_discovery_or_ctest_metadata"
    if any("tools/CMakeFiles/gentest_codegen.dir" in output or output.endswith("/tools/gentest_codegen") for output in outputs):
        return "generator_tool"
    return "other"


def infer_target(outputs: list[str]) -> str:
    for output in outputs:
        parts = output.split("/")
        if output.startswith("tests/CMakeFiles/") and len(parts) > 2:
            return parts[2].removesuffix(".dir")
        if output.startswith("tests/gentest_") and len(parts) == 2:
            return parts[1]
        if output.startswith("tests/") and len(parts) > 2:
            suite = parts[1]
            if suite == "generated":
                return "gentest_textual_suite_mocks"
            return f"gentest_{suite}_tests"
        if output.startswith("src/"):
            return "runtime"
    return "other"


def summarize_ninja_log(build_dir: Path) -> dict[str, object]:
    ninja_log = build_dir / ".ninja_log"
    summary: dict[str, object] = {"output_entries": 0, "unique_edges": 0, "wall_s": 0.0, "categories": {}, "targets": {}, "top_edges": []}
    if not ninja_log.exists():
        return summary

    edges: dict[tuple[int, int, str], list[str]] = {}
    for line in ninja_log.read_text(encoding="utf-8").splitlines():
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
    targets: dict[str, dict[str, float | int]] = {}
    top_edges: list[dict[str, object]] = []
    output_entries = 0
    wall_ms = 0
    for key, outputs in edges.items():
        start_ms, end_ms, _ = key
        wall_ms = max(wall_ms, end_ms)
        output_entries += len(outputs)
        duration_s = (end_ms - start_ms) / 1000.0
        category = classify_ninja_outputs(outputs)
        target = infer_target(outputs)

        item = categories.setdefault(category, {"count": 0, "summed_edge_s": 0.0, "max_edge_s": 0.0})
        item["count"] = int(item["count"]) + 1
        item["summed_edge_s"] = float(item["summed_edge_s"]) + duration_s
        item["max_edge_s"] = max(float(item["max_edge_s"]), duration_s)

        target_item = targets.setdefault(target, {"summed_edge_s": 0.0, "count": 0})
        target_item["count"] = int(target_item["count"]) + 1
        target_item["summed_edge_s"] = float(target_item["summed_edge_s"]) + duration_s

        top_edges.append(
            {
                "category": category,
                "target": target,
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
    summary["targets"] = targets
    summary["top_edges"] = sorted(top_edges, key=lambda edge: float(edge["duration_s"]), reverse=True)[:20]
    return summary


def stats(samples: list[float]) -> dict[str, object]:
    return {
        "samples_s": samples,
        "median_s": statistics.median(samples),
        "mean_s": statistics.mean(samples),
        "min_s": min(samples),
        "max_s": max(samples),
    }


def parse_targets(raw: str | None) -> list[str]:
    if not raw:
        return list(DEFAULT_TARGETS)
    targets = [target.strip() for target in raw.split(",") if target.strip()]
    if not targets:
        raise ValueError("--targets did not contain any target names")
    return targets


def print_summary(result: dict[str, object]) -> None:
    current = result["current"]
    base = result["base"]
    delta = result["delta"]
    assert isinstance(current, dict)
    assert isinstance(base, dict)
    assert isinstance(delta, dict)

    print("\n=== Release E2E Benchmark Compare (gentest_codegen build excluded) ===")
    print(f"Current: {current['sha']} ({current['source_dir']})")
    print(f"Base:    {base['sha']} ({result['base_ref']})")
    print(f"Jobs:    {result['jobs']}")
    print(f"Targets: {', '.join(result['targets'])}")
    print()
    print(f"{'Ref':<10} | {'Median (s)':>10} | {'Mean (s)':>10} | Samples")
    print("-" * 72)
    print(f"{'current':<10} | {current['median_s']:>10.3f} | {current['mean_s']:>10.3f} | {', '.join(f'{v:.3f}' for v in current['samples_s'])}")
    print(f"{'base':<10} | {base['median_s']:>10.3f} | {base['mean_s']:>10.3f} | {', '.join(f'{v:.3f}' for v in base['samples_s'])}")
    print()
    print(f"Delta (current - base): {delta['median_s']:+.3f}s ({delta['median_pct']:+.2f}%)")
    ninja_log = current.get("ninja_log")
    if isinstance(ninja_log, dict) and ninja_log.get("categories"):
        print("\nCurrent Timed Build Profile")
        print(f"Wall from Ninja log: {float(ninja_log.get('wall_s', 0.0)):.3f}s")
        print(f"{'Category':<32} | {'Edges':>5} | {'Sum edge-s':>10} | {'Max edge-s':>10}")
        print("-" * 68)
        categories = ninja_log.get("categories", {})
        assert isinstance(categories, dict)
        for name, data in sorted(categories.items(), key=lambda item: float(item[1].get("summed_edge_s", 0.0)), reverse=True):
            assert isinstance(data, dict)
            print(
                f"{name:<32} | {int(data.get('count', 0)):>5} | "
                f"{float(data.get('summed_edge_s', 0.0)):>10.3f} | {float(data.get('max_edge_s', 0.0)):>10.3f}"
            )


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--base-ref", default="origin/master", help="Base branch, tag, or commit to compare against (default: origin/master)")
    ap.add_argument("--fetch", action="store_true", help="Run `git fetch origin` before resolving the base ref")
    ap.add_argument("--samples", type=int, default=3, help="Number of paired clean build samples to run")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 1, help="Parallel build jobs")
    ap.add_argument("--build-type", default="Release", help="CMake build type for tool and timed builds")
    ap.add_argument("--targets", default=None, help="Comma-separated target list; defaults to representative generated test binaries")
    ap.add_argument("--cmake-arg", action="append", default=[], help="Extra CMake argument for timed test build configure")
    ap.add_argument("--output", default=None, help="JSON output path")
    ap.add_argument("--ccache", choices=["off", "on"], default="off", help="Disable ccache by default for stable timings")
    args = ap.parse_args()

    if args.samples < 1:
        print("error: --samples must be at least 1", file=sys.stderr)
        return 2
    if args.jobs < 1:
        print("error: --jobs must be at least 1", file=sys.stderr)
        return 2

    root = repo_root()
    if args.fetch:
        log("[bench] Fetching origin")
        run(["git", "fetch", "origin"], cwd=root)

    targets = parse_targets(args.targets)
    current_sha = git(root, "rev-parse", "HEAD")
    base_sha = git(root, "rev-parse", args.base_ref)

    env = os.environ.copy()
    if args.ccache == "off":
        env["CCACHE_DISABLE"] = "1"

    if args.output:
        output = Path(args.output)
        if not output.is_absolute():
            output = root / output
    else:
        output = root / "build" / "bench-release-e2e-compare" / "release_e2e_compare.json"
    output.parent.mkdir(parents=True, exist_ok=True)

    tmp = tempfile.TemporaryDirectory(prefix="gentest-release-e2e-base-")
    base_dir = Path(tmp.name) / "base"
    worktree_added = False
    try:
        log(f"[bench] Creating base worktree: {args.base_ref} ({base_sha[:12]})")
        run(["git", "worktree", "add", "--detach", str(base_dir), base_sha], cwd=root)
        worktree_added = True

        current_tool_build = root / "build" / "bench-release-e2e-compare" / "current-tool"
        current_timed_build = root / "build" / "bench-release-e2e-compare" / "current"
        base_tool_build = base_dir / "build" / "bench-release-e2e-compare" / "base-tool"
        base_timed_build = base_dir / "build" / "bench-release-e2e-compare" / "base"

        current_codegen = configure_and_build_tool(root, current_tool_build, args.build_type, args.jobs, env)
        base_codegen = configure_and_build_tool(base_dir, base_tool_build, args.build_type, args.jobs, env)

        configure_timed_build(root, current_timed_build, args.build_type, current_codegen, args.cmake_arg, env)
        configure_timed_build(base_dir, base_timed_build, args.build_type, base_codegen, args.cmake_arg, env)

        current_samples: list[float] = []
        base_samples: list[float] = []
        for sample in range(1, args.samples + 1):
            current_samples.append(clean_build_sample("current", root, current_timed_build, targets, args.jobs, env, sample))
            base_samples.append(clean_build_sample("base", base_dir, base_timed_build, targets, args.jobs, env, sample))

        current = {
            "source_dir": str(root),
            "sha": current_sha,
            "tool_build_dir": str(current_tool_build),
            "timed_build_dir": str(current_timed_build),
            **stats(current_samples),
            "ninja_log": summarize_ninja_log(current_timed_build),
        }
        base = {
            "source_dir": str(base_dir),
            "sha": base_sha,
            "tool_build_dir": str(base_tool_build),
            "timed_build_dir": str(base_timed_build),
            **stats(base_samples),
            "ninja_log": summarize_ninja_log(base_timed_build),
        }

        median_delta = float(current["median_s"]) - float(base["median_s"])
        median_pct = (median_delta / float(base["median_s"]) * 100.0) if float(base["median_s"]) else 0.0
        result: dict[str, object] = {
            "base_ref": args.base_ref,
            "build_type": args.build_type,
            "jobs": args.jobs,
            "samples": args.samples,
            "ccache": args.ccache,
            "targets": targets,
            "generator_tool_build_excluded": True,
            "current": current,
            "base": base,
            "delta": {"median_s": median_delta, "median_pct": median_pct},
        }

        output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        log(f"[bench] Wrote {output}")
        print_summary(result)
    finally:
        if worktree_added:
            run(["git", "worktree", "remove", "--force", str(base_dir)], cwd=root)
        tmp.cleanup()
        # Git leaves empty parent directories behind when a failed worktree add
        # never registered; keep the temp area tidy in that case too.
        if base_dir.parent.exists():
            shutil.rmtree(base_dir.parent, ignore_errors=True)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
