#!/usr/bin/env python3
"""
Configure a clean build dir and run compile benchmarks.

Usage examples:
  python3 scripts/bench_clean.py
  python3 scripts/bench_clean.py --preset release --jobs 4
  python3 scripts/bench_clean.py --codegen-only --no-clean
"""
import argparse
import subprocess
import sys
from pathlib import Path


def run(cmd, **kwargs):
    return subprocess.run(cmd, check=True, **kwargs)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preset", default="debug-system", help="CMake configure preset to use")
    ap.add_argument("--jobs", type=int, default=1)
    ap.add_argument("--codegen-only", action="store_true")
    ap.add_argument("--no-clean", action="store_true")
    args = ap.parse_args()

    run(["cmake", f"--preset={args.preset}", "--fresh"])

    build_dir = Path("build") / args.preset
    bench_script = Path(__file__).resolve().parent / "bench_compile.py"
    cmd = [sys.executable, str(bench_script), "--build-dir", str(build_dir), "--jobs", str(args.jobs)]
    if args.codegen_only:
        cmd.append("--codegen-only")
    if args.no_clean:
        cmd.append("--no-clean")
    run(cmd)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
