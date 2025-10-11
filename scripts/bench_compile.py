#!/usr/bin/env python3
"""
Benchmark compile times for gentest components.

- Measures time to build the generator tool (gentest_codegen) separately.
- Measures time to build each test target (e.g., gentest_unit_tests) with optional clean.

Usage examples:
  python3 scripts/bench_compile.py --build-dir build/debug-system
  python3 scripts/bench_compile.py --build-dir build/debug --jobs 4 --no-clean

Options:
  --build-dir   Path to an existing CMake build tree (any preset/config)
  --config      Multi-config setting (e.g., Debug, Release) when using MSVC/Xcode
  --jobs        Parallel jobs to pass to cmake --build (default: 1 for stability)
  --no-clean    Do not pass --clean-first for each timed build
  --targets     Explicit targets to time (comma-separated). If omitted, targets
                are discovered from `cmake --build --target help` and filtered
                to names ending in `_tests`, plus `gentest_concurrency_tests`.

Writes results to <build-dir>/compile_bench.json and prints a summary.
"""
import argparse
import json
import os
import re
import shlex
import subprocess
import sys
import time
from pathlib import Path


def run(cmd, **kwargs):
    return subprocess.run(cmd, check=True, **kwargs)


def cmake_build(build_dir, target=None, jobs=1, clean_first=True, config=None):
    cmd = ["cmake", "--build", str(build_dir)]
    if target:
        cmd += ["--target", target]
    if config:
        cmd += ["--config", config]
    if clean_first:
        cmd += ["--clean-first"]
    if jobs:
        cmd += ["-j", str(jobs)]
    start = time.perf_counter()
    proc = run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    end = time.perf_counter()
    return end - start, proc


def discover_test_targets(build_dir, config=None):
    # Ask CMake for target help and collect likely test targets ending with _tests
    cmd = ["cmake", "--build", str(build_dir), "--target", "help"]
    if config:
        cmd += ["--config", config]
    try:
        out = subprocess.run(cmd, check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True).stdout
    except subprocess.CalledProcessError:
        return []
    candidates = set()
    for line in out.splitlines():
        m = re.search(r"\b(gentest_\w+_tests)\b", line)
        if m:
            candidates.add(m.group(1))
    # stable ordering
    return sorted(candidates)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build-dir", required=True)
    ap.add_argument("--config", default=None)
    ap.add_argument("--jobs", type=int, default=1)
    ap.add_argument("--no-clean", action="store_true")
    ap.add_argument("--targets", default=None, help="Comma-separated target names to build and time")
    args = ap.parse_args()

    build_dir = Path(args.build_dir)
    if not build_dir.exists():
        print(f"error: build dir not found: {build_dir}", file=sys.stderr)
        return 2

    # Time generator tool build separately
    print("[bench] Building generator tool: gentest_codegen ...")
    gen_time, _ = cmake_build(build_dir, target="gentest_codegen", jobs=args.jobs, clean_first=not args.no_clean, config=args.config)
    print(f"[bench] gentest_codegen: {gen_time:.3f}s")

    # Time test targets
    if args.targets:
        targets = [t.strip() for t in args.targets.split(',') if t.strip()]
    else:
        targets = discover_test_targets(build_dir, args.config)
        if not targets:
            # fallback to known names
            targets = [
                "gentest_unit_tests",
                "gentest_integration_tests",
                "gentest_failing_tests",
                "gentest_skiponly_tests",
                "gentest_fixtures_tests",
                "gentest_templates_tests",
                "gentest_mocking_tests",
                "gentest_concurrency_tests",
            ]
    print(f"[bench] Test targets: {', '.join(targets)}")

    results = {
        "build_dir": str(build_dir),
        "config": args.config,
        "jobs": args.jobs,
        "clean_first": not args.no_clean,
        "generator": {"target": "gentest_codegen", "elapsed_s": gen_time},
        "targets": [],
        "total_elapsed_s": 0.0,
    }

    total = 0.0
    for t in targets:
        print(f"[bench] Building {t} ...")
        elapsed, _ = cmake_build(build_dir, target=t, jobs=args.jobs, clean_first=not args.no_clean, config=args.config)
        print(f"[bench] {t}: {elapsed:.3f}s")
        results["targets"].append({"target": t, "elapsed_s": elapsed})
        total += elapsed

    results["total_elapsed_s"] = total
    out_path = build_dir / "compile_bench.json"
    with out_path.open("w", encoding="utf-8") as f:
        json.dump(results, f, indent=2)
    print(f"[bench] Wrote {out_path}")

    # Human summary
    print("\n=== Compile Benchmark Summary ===")
    print(f"Build dir: {build_dir}")
    if args.config:
        print(f"Config:    {args.config}")
    print(f"Jobs:      {args.jobs}")
    print(f"Clean:     {not args.no_clean}")
    print(f"Generator: {gen_time:.3f}s")
    for entry in results["targets"]:
        print(f"{entry['target']:<32} {entry['elapsed_s']:.3f}s")
    print(f"Total (targets):         {total:.3f}s")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

