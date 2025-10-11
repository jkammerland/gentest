#!/usr/bin/env python3
"""
Compare compile times between two configurations (build-dirs or presets).

Runs bench_compile.py for both configs if needed, then reads their JSON
results and prints a side-by-side summary table:

  - Generator compile time
  - Codegen (sum)
  - Targets total (sum of per-target build times)

Usage examples:
  # Compare a Debug build-dir vs Release preset
  python3 scripts/bench_compare.py --a-build-dir build/debug-system --b-preset release --jobs 1 --no-clean

  # Compare two presets
  python3 scripts/bench_compare.py --a-preset debug --b-preset release

Outputs a human readable table.
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path


def run_bench(build_dir=None, preset=None, jobs=1, no_clean=False):
    cmd = ["python3", "scripts/bench_compile.py"]
    if preset:
        cmd += ["--preset", preset]
        out_dir = Path("build") / preset
    else:
        cmd += ["--build-dir", str(build_dir)]
        out_dir = Path(build_dir)
    cmd += ["--jobs", str(jobs)]
    if no_clean:
        cmd += ["--no-clean"]
    subprocess.run(cmd, check=True)
    return out_dir / "compile_bench.json"


def load_results(path: Path):
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--a-build-dir", default=None)
    ap.add_argument("--a-preset", default=None)
    ap.add_argument("--b-build-dir", default=None)
    ap.add_argument("--b-preset", default=None)
    ap.add_argument("--jobs", type=int, default=1)
    ap.add_argument("--no-clean", action="store_true")
    args = ap.parse_args()

    if not (args.a_build_dir or args.a_preset):
        print("error: need --a-build-dir or --a-preset", file=sys.stderr)
        return 2
    if not (args.b_build_dir or args.b_preset):
        print("error: need --b-build-dir or --b-preset", file=sys.stderr)
        return 2

    a_json = run_bench(build_dir=args.a_build_dir, preset=args.a_preset, jobs=args.jobs, no_clean=args.no_clean)
    b_json = run_bench(build_dir=args.b_build_dir, preset=args.b_preset, jobs=args.jobs, no_clean=args.no_clean)

    a = load_results(a_json)
    b = load_results(b_json)

    def label(res):
        if res.get("build_dir") and res["build_dir"] != "None":
            return res["build_dir"]
        # try preset path part of json location
        return str(a_json.parent) if res is a else str(b_json.parent)

    # Summaries
    rows = [
        ("Generator compile", a["generator"]["elapsed_s"], b["generator"]["elapsed_s"]),
        ("Codegen (sum)", a.get("generation", {}).get("total_elapsed_s", 0.0), b.get("generation", {}).get("total_elapsed_s", 0.0)),
        ("Targets total", a.get("total_elapsed_s", 0.0), b.get("total_elapsed_s", 0.0)),
    ]

    # Print table
    print("\n=== Compile Benchmark Compare ===")
    print(f"A: {label(a)}")
    print(f"B: {label(b)}\n")
    header = f"{'Metric':<20} | {'A (s)':>10} | {'B (s)':>10} | Delta (B-A)"
    print(header)
    print("-" * len(header))
    for name, av, bv in rows:
        delta = bv - av
        print(f"{name:<20} | {av:>10.3f} | {bv:>10.3f} | {delta:+.3f}")
    print()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

