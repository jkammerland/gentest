#!/usr/bin/env python3
"""
Benchmark compile times for gentest components.

- Measures time to build the generator tool (gentest_codegen) separately.
- Measures time to build each test target (e.g., gentest_unit_tests) with optional clean.

Usage examples:
  python3 scripts/bench_compile.py --build-dir build/debug-system
  python3 scripts/bench_compile.py --build-dir build/debug --jobs 4 --no-clean
  python3 scripts/bench_compile.py --preset release                 # use a CMake preset

Options:
  --build-dir   Path to an existing CMake build tree (any preset/config)
  --config      Multi-config setting (e.g., Debug, Release) when using MSVC/Xcode
  --jobs        Parallel jobs to pass to cmake --build (default: 1 for stability)
  --codegen-jobs  Override the configured gentest_codegen --jobs=<N> cap (0/auto=auto)
  --no-clean    Do not pass --clean-first for each timed build
  --codegen-only  Only time codegen commands (skip building test targets)
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

from compile_bench_common import (
    CodegenCommandError,
    codegen_job_values,
    median_mad,
    ninja_codegen_command_file,
    parse_codegen_jobs,
    rewrite_codegen_jobs,
    temporary_ninja_codegen_commands,
)


def run(cmd, **kwargs):
    return subprocess.run(cmd, check=True, **kwargs)


def cmake_build(build_dir, target=None, jobs=1, clean_first=True, config=None, preset=None):
    if preset:
        cmd = ["cmake", "--build", "--preset", preset]
    else:
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


def discover_test_targets(build_dir, config=None, preset=None):
    # Ask CMake for target help and collect likely test targets ending with _tests
    if preset:
        cmd = ["cmake", "--build", "--preset", preset, "--target", "help"]
    else:
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


def timed_samples(action, warmups: int, samples: int) -> list[float]:
    for _ in range(warmups):
        action()
    values: list[float] = []
    for _ in range(samples):
        start = time.perf_counter()
        action()
        values.append(time.perf_counter() - start)
    return values


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build-dir", default=None)
    ap.add_argument("--preset", default=None, help="CMake preset name to build/time")
    ap.add_argument("--config", default=None)
    ap.add_argument("--jobs", type=int, default=1)
    ap.add_argument("--codegen-jobs", default=None, help="Override configured gentest_codegen --jobs=<N> (non-negative integer or auto)")
    ap.add_argument("--warmups", type=int, default=2, help="Untimed warmup runs before each measurement (default: 2)")
    ap.add_argument("--samples", type=int, default=7, help="Raw timed samples per measurement (default: 7)")
    ap.add_argument("--no-clean", action="store_true")
    ap.add_argument("--codegen-only", action="store_true")
    ap.add_argument("--targets", default=None, help="Comma-separated target names to build and time")
    args = ap.parse_args()

    if args.jobs < 1:
        print("error: --jobs must be at least 1", file=sys.stderr)
        return 2
    if args.warmups < 0 or args.samples < 1:
        print("error: --warmups must be non-negative and --samples must be at least 1", file=sys.stderr)
        return 2
    if args.codegen_jobs is not None:
        try:
            parse_codegen_jobs(args.codegen_jobs)
        except CodegenCommandError as error:
            print(f"error: {error}", file=sys.stderr)
            return 2

    if not args.preset:
        if not args.build_dir:
            print("error: either --build-dir or --preset must be provided", file=sys.stderr)
            return 2
        build_dir = Path(args.build_dir)
        if not build_dir.exists():
            print(f"error: build dir not found: {build_dir}", file=sys.stderr)
            return 2
    else:
        build_dir = None

    configured_build = Path(args.build_dir) if args.build_dir else Path("build") / args.preset
    build_ninja = ninja_codegen_command_file(configured_build / "build.ninja", args.config)
    ninja_cap_rewrites = []
    if args.codegen_jobs is not None:
        try:
            # Verify every configured command can take the requested cap, then
            # immediately restore the caller's generated build file.
            with temporary_ninja_codegen_commands(build_ninja, args.codegen_jobs) as rewrites:
                ninja_cap_rewrites = rewrites
        except CodegenCommandError as error:
            print(f"error: {error}", file=sys.stderr)
            return 2

    def capped_cmake_build(target: str, *, clean_first: bool):
        if args.codegen_jobs is None:
            return cmake_build(
                build_dir,
                target=target,
                jobs=args.jobs,
                clean_first=clean_first,
                config=args.config,
                preset=args.preset,
            )
        with temporary_ninja_codegen_commands(build_ninja, args.codegen_jobs):
            return cmake_build(
                build_dir,
                target=target,
                jobs=args.jobs,
                clean_first=clean_first,
                config=args.config,
                preset=args.preset,
            )

    # Time generator tool build separately
    print("[bench] Building generator tool: gentest_codegen ...")
    def build_generator():
        cmake_build(build_dir, target="gentest_codegen", jobs=args.jobs, clean_first=not args.no_clean, config=args.config, preset=args.preset)
    generator_stats = median_mad(timed_samples(build_generator, args.warmups, args.samples))
    gen_time = float(generator_stats["median_s"])
    print(f"[bench] gentest_codegen median: {gen_time:.3f}s (MAD {float(generator_stats['mad_s']):.3f}s)")

    # Time test targets
    if args.targets:
        targets = [t.strip() for t in args.targets.split(',') if t.strip()]
    else:
        targets = discover_test_targets(build_dir, args.config) if not args.preset else discover_test_targets(None, None, preset=args.preset)
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
        "codegen_jobs": args.codegen_jobs,
        "codegen_ninja_rewrites": ninja_cap_rewrites,
        "warmups": args.warmups,
        "samples": args.samples,
        "clean_first": not args.no_clean,
        "codegen_only": args.codegen_only,
        "generator": {"target": "gentest_codegen", "elapsed_s": gen_time, **generator_stats},
        "generation": {"targets": [], "total_elapsed_s": 0.0},
        "targets": [],
        "total_elapsed_s": 0.0,
    }

    # Stage 2: time code generation by executing the exact gentest_codegen commands from build.ninja
    def parse_generation_commands(bdir: Path):
        buildfile = ninja_codegen_command_file(
            (bdir / "build.ninja") if bdir else (Path("build") / args.preset / "build.ninja"), args.config
        )
        if not buildfile.exists():
            return {}
        lines = buildfile.read_text().splitlines()
        mapping = {}
        current_cmd = None
        current_impl = None
        running_re = re.compile(r"^\s*DESC\s*=\s*Running gentest_codegen for target\s+([\w_]+)\s*$")
        legacy_re = re.compile(r"^\s*DESC\s*=\s*Generating\s+(.+?/tests/[^/]+/test_impl\\.cpp)\s+for\s+target\s+([\w_]+)\s*$")
        cmd_re = re.compile(r"^\s*COMMAND\s*=\s*(.+)$")

        for line in lines:
            if line.startswith("build "):
                current_cmd = None
                current_impl = None
                continue
            cmd_match = cmd_re.match(line)
            if cmd_match:
                current_cmd = cmd_match.group(1)
                continue
            legacy_match = legacy_re.match(line)
            if legacy_match:
                current_impl = legacy_match.group(1)
                target_name = legacy_match.group(2)
                if current_cmd:
                    mapping[target_name] = {"impl": current_impl, "command": current_cmd}
                continue
            running_match = running_re.match(line)
            if running_match:
                target_name = running_match.group(1)
                if current_cmd:
                    mapping[target_name] = {"impl": current_impl, "command": current_cmd}
                continue
        return mapping

    gen_cmds = parse_generation_commands(Path(args.build_dir) if args.build_dir else None)
    gen_total = 0.0
    gen_env = os.environ.copy()
    for t in targets:
        info = gen_cmds.get(t)
        if not info:
            continue
        # Materialise all declared inputs (including explicit mock surfaces)
        # once outside the timed direct-codegen samples.  The later cold E2E
        # target samples clean the build independently.
        print(f"[bench] Preparing declared inputs for {t} ...")
        capped_cmake_build(t, clean_first=False)
        command = info["command"]
        try:
            if args.codegen_jobs is not None:
                command, cap = rewrite_codegen_jobs(command, args.codegen_jobs)
            else:
                cap = {"requested": None, "effective_values": codegen_job_values(command)}
        except CodegenCommandError as error:
            print(f"error: {t}: {error}", file=sys.stderr)
            return 2
        print(f"[bench] Generating sources for {t} ...")
        def generate():
            # Run the command in a shell to honor CMake's cd && ... chains.
            subprocess.run(command, shell=True, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=gen_env)
        stats = median_mad(timed_samples(generate, args.warmups, args.samples))
        elapsed = float(stats["median_s"])
        print(f"[bench] gen[{t}] median: {elapsed:.3f}s (MAD {float(stats['mad_s']):.3f}s)")
        results["generation"]["targets"].append({"target": t, "elapsed_s": elapsed, "codegen_cap": cap, **stats})
        gen_total += elapsed
    results["generation"]["total_elapsed_s"] = gen_total

    # Stage 3: target E2E builds.  Cold samples intentionally include codegen;
    # they are not reported as compile-only or added to the component total.
    total = 0.0
    if args.codegen_only:
        print("[bench] Skipping test target builds (--codegen-only).")
    else:
        for t in targets:
            print(f"[bench] Building {t} ...")
            def build_target():
                capped_cmake_build(t, clean_first=not args.no_clean)
            stats = median_mad(timed_samples(build_target, args.warmups, args.samples))
            elapsed = float(stats["median_s"])
            print(f"[bench] {t} median: {elapsed:.3f}s (MAD {float(stats['mad_s']):.3f}s)")
            results["targets"].append({
                "target": t,
                "measurement": "cold-e2e" if not args.no_clean else "no-op-e2e",
                "includes_codegen": not args.no_clean,
                "elapsed_s": elapsed,
                **stats,
            })
            total += elapsed

    results["total_elapsed_s"] = total
    out_dir = Path(args.build_dir) if args.build_dir else Path("build")/args.preset
    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / "compile_bench.json"
    with out_path.open("w", encoding="utf-8") as f:
        json.dump(results, f, indent=2)
    print(f"[bench] Wrote {out_path}")

    # Human summary
    print("\n=== Compile Benchmark Summary ===")
    print(f"Build dir: {build_dir or '(preset: ' + args.preset + ')'}")
    if args.config:
        print(f"Config:    {args.config}")
    print(f"Jobs:      {args.jobs}")
    print(f"Codegen jobs: {args.codegen_jobs if args.codegen_jobs is not None else '(configured default)'}")
    print(f"Warmups/samples: {args.warmups}/{args.samples}")
    print(f"Clean:     {not args.no_clean}")
    print(f"Generator compile:       {gen_time:.3f}s")
    print(f"Codegen (sum):           {gen_total:.3f}s")
    if args.codegen_only:
        print("Total (targets):         skipped (--codegen-only)")
    else:
        for entry in results["targets"]:
            print(f"{entry['target']:<32} {entry['elapsed_s']:.3f}s")
        print(f"Total (targets):         {total:.3f}s")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
