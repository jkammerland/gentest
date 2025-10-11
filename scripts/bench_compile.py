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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build-dir", default=None)
    ap.add_argument("--preset", default=None, help="CMake preset name to build/time")
    ap.add_argument("--config", default=None)
    ap.add_argument("--jobs", type=int, default=1)
    ap.add_argument("--no-clean", action="store_true")
    ap.add_argument("--targets", default=None, help="Comma-separated target names to build and time")
    args = ap.parse_args()

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

    # Time generator tool build separately
    print("[bench] Building generator tool: gentest_codegen ...")
    gen_time, _ = cmake_build(build_dir, target="gentest_codegen", jobs=args.jobs, clean_first=not args.no_clean, config=args.config, preset=args.preset)
    print(f"[bench] gentest_codegen: {gen_time:.3f}s")

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
        "clean_first": not args.no_clean,
        "generator": {"target": "gentest_codegen", "elapsed_s": gen_time},
        "generation": {"targets": [], "total_elapsed_s": 0.0},
        "targets": [],
        "total_elapsed_s": 0.0,
    }

    # Stage 2: time code generation by executing the exact gentest_codegen commands from build.ninja
    def parse_generation_commands(bdir: Path):
        buildfile = (bdir / "build.ninja") if bdir else (Path("build")/args.preset/"build.ninja")
        if not buildfile.exists():
            return {}
        text = buildfile.read_text()
        # Map: target -> command string
        mapping = {}
        # Find blocks by DESC lines
        for m in re.finditer(r"^\s*DESC\s*=\s*Generating\s+(.+?/tests/[^/]+/test_impl\\.cpp)\s+for\s+target\s+([\w_]+)\s*$", text, re.MULTILINE):
            test_impl_path = m.group(1)
            target_name = m.group(2)
            # Walk backwards to find the preceding COMMAND line in this block
            block_start = text.rfind("\n", 0, m.start())
            block = text[text.rfind("\n\n", 0, m.start())+1:m.start()] if text.rfind("\n\n", 0, m.start()) != -1 else text[:m.start()]
            cmd_match = re.search(r"^\s*COMMAND\s*=\s*(.+)$", block, re.MULTILINE)
            if not cmd_match:
                continue
            command = cmd_match.group(1)
            mapping[target_name] = {"impl": test_impl_path, "command": command}
        return mapping

    gen_cmds = parse_generation_commands(Path(args.build_dir) if args.build_dir else None)
    gen_total = 0.0
    for t in targets:
        info = gen_cmds.get(t)
        if not info:
            continue
        print(f"[bench] Generating sources for {t} ...")
        start = time.perf_counter()
        # Run the command in a shell to honor && and quoting
        subprocess.run(info["command"], shell=True, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        elapsed = time.perf_counter() - start
        print(f"[bench] gen[{t}]: {elapsed:.3f}s")
        results["generation"]["targets"].append({"target": t, "elapsed_s": elapsed})
        gen_total += elapsed
    results["generation"]["total_elapsed_s"] = gen_total

    # Stage 3: compile tests (no clean so we don't re-run generation)
    total = 0.0
    for t in targets:
        print(f"[bench] Building {t} ...")
        elapsed, _ = cmake_build(build_dir, target=t, jobs=args.jobs, clean_first=False if gen_cmds else (not args.no_clean), config=args.config, preset=args.preset)
        print(f"[bench] {t}: {elapsed:.3f}s")
        results["targets"].append({"target": t, "elapsed_s": elapsed})
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
    print(f"Clean:     {not args.no_clean}")
    print(f"Generator compile:       {gen_time:.3f}s")
    print(f"Codegen (sum):           {gen_total:.3f}s")
    for entry in results["targets"]:
        print(f"{entry['target']:<32} {entry['elapsed_s']:.3f}s")
    print(f"Total (targets):         {total:.3f}s")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
