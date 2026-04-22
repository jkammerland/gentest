#!/usr/bin/env python3
"""
Scale synthetic one-TU test count against gentest, GoogleTest, and doctest.

The script generates a tiny CMake project under the build tree for each case
count, then measures consumer target rebuilds after prebuilding gentest runtime
and gentest_codegen. Configure, framework/runtime builds, and the generator tool
build are excluded from the timed section.
The prebuilt gentest_codegen host tool defaults to a Release build even when the
consumer target build type is Debug; override with --codegen-build-type when
you intentionally want to measure a differently built host tool.

Usage:
  python3 scripts/bench_case_scale.py --counts 1,10,100,500,1000 --samples 3 --jobs 8
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
    log(f"[scale] Configuring prebuilt gentest_codegen: {build_dir}")
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
    log("[scale] Building prebuilt gentest_codegen")
    run(["cmake", "--build", str(build_dir), "--target", "gentest_codegen", "-j", str(jobs)], cwd=root, env=env)
    exe = build_dir / "tools" / ("gentest_codegen.exe" if os.name == "nt" else "gentest_codegen")
    if not exe.exists():
        raise FileNotFoundError(f"gentest_codegen was not produced at {exe}")
    return exe


def write_scale_project(source_dir: Path, root: Path, count: int, doctest_repository: str, doctest_tag: str) -> None:
    source_dir.mkdir(parents=True, exist_ok=True)
    (source_dir / "CMakeLists.txt").write_text(
        f"""cmake_minimum_required(VERSION 3.24)
project(gentest_gtest_scale_{count} LANGUAGES C CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(gentest_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(GENTEST_BUILD_CODEGEN OFF CACHE BOOL "" FORCE)

add_subdirectory("{root}" gentest EXCLUDE_FROM_ALL)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTEST REQUIRED IMPORTED_TARGET gtest gtest_main)

include(FetchContent)
set(GENTEST_SCALE_DOCTEST_REPOSITORY "{doctest_repository}" CACHE STRING "doctest repository used by the scale harness")
set(GENTEST_SCALE_DOCTEST_TAG "{doctest_tag}" CACHE STRING "doctest commit, tag, or branch used by the scale harness")
FetchContent_Declare(
    doctest_scale
    GIT_REPOSITORY "${{GENTEST_SCALE_DOCTEST_REPOSITORY}}"
    GIT_TAG "${{GENTEST_SCALE_DOCTEST_TAG}}"
)
FetchContent_MakeAvailable(doctest_scale)

add_executable(gentest_scale gentest_cases.cpp)
target_link_libraries(gentest_scale PRIVATE gentest_main)
target_compile_features(gentest_scale PRIVATE cxx_std_20)
target_include_directories(gentest_scale PRIVATE "{root}/include")
gentest_attach_codegen(gentest_scale
    OUTPUT_DIR "${{CMAKE_CURRENT_BINARY_DIR}}/gentest_generated"
    CLANG_ARGS
        -std=c++20
        "-I{root}/include")

add_executable(gtest_scale gtest_cases.cpp)
target_link_libraries(gtest_scale PRIVATE PkgConfig::GTEST)
target_compile_features(gtest_scale PRIVATE cxx_std_20)

add_library(doctest_scale_main STATIC doctest_main.cpp)
target_link_libraries(doctest_scale_main PUBLIC doctest::doctest)
target_compile_features(doctest_scale_main PUBLIC cxx_std_20)

add_executable(doctest_scale doctest_cases.cpp)
target_link_libraries(doctest_scale PRIVATE doctest_scale_main)
target_compile_features(doctest_scale PRIVATE cxx_std_20)
""",
        encoding="utf-8",
    )

    gentest_lines = [
        '#include "gentest/attributes.h"',
        '#include "gentest/runner.h"',
        "using namespace gentest::asserts;",
        "",
        "namespace scale {",
        "",
    ]
    for idx in range(count):
        gentest_lines.extend(
            [
                f'[[using gentest: test("case/{idx:06d}")]]',
                f"void case_{idx:06d}() {{",
                f"    EXPECT_EQ({idx}, {idx});",
                "}",
                "",
            ]
        )
    gentest_lines.append("} // namespace scale\n")
    (source_dir / "gentest_cases.cpp").write_text("\n".join(gentest_lines), encoding="utf-8")

    gtest_lines = [
        "#include <gtest/gtest.h>",
        "",
        "namespace scale {",
        "",
    ]
    for idx in range(count):
        gtest_lines.extend(
            [
                f"TEST(Scale, Case{idx:06d}) {{",
                f"    EXPECT_EQ({idx}, {idx});",
                "}",
                "",
            ]
        )
    gtest_lines.append("} // namespace scale\n")
    (source_dir / "gtest_cases.cpp").write_text("\n".join(gtest_lines), encoding="utf-8")

    (source_dir / "doctest_compat.hpp").write_text(
        """#pragma once

// jkammerland/doctest@397ce008 uses std::tuple_cat without including <tuple>.
// Keep the compatibility include local to generated scale fixtures.
// clang-format off
#include <tuple>
#include <doctest/doctest.h>
// clang-format on
""",
        encoding="utf-8",
    )
    (source_dir / "doctest_main.cpp").write_text(
        """#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest_compat.hpp"
""",
        encoding="utf-8",
    )

    doctest_lines = [
        '#include "doctest_compat.hpp"',
        "",
        "namespace scale {",
        "",
    ]
    for idx in range(count):
        doctest_lines.extend(
            [
                f'TEST_CASE("case/{idx:06d}") {{',
                f"    CHECK_EQ({idx}, {idx});",
                "}",
                "",
            ]
        )
    doctest_lines.append("} // namespace scale\n")
    (source_dir / "doctest_cases.cpp").write_text("\n".join(doctest_lines), encoding="utf-8")


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

    if target == "gentest_scale":
        generated_dir = build_dir / "gentest_generated"
        for pattern in ("*.gentest.h", "*.artifact_manifest.json", "*.artifact_manifest.validated"):
            for path in generated_dir.glob(pattern):
                path.unlink()


def ninja_log_line_count(ninja_log: Path) -> int:
    if not ninja_log.exists():
        return 0
    with ninja_log.open("r", encoding="utf-8") as f:
        return sum(1 for _ in f)


def classify_outputs(outputs: list[str]) -> str:
    if any(output.endswith(".artifact_manifest.validated") for output in outputs):
        return "gentest_manifest_validation"
    if any(output.endswith(".gentest.h") or output.endswith(".gentest.cpp") for output in outputs):
        return "gentest_codegen"
    if any(output.endswith(".o") and "gentest_scale.dir" in output for output in outputs):
        return "gentest_tu_compile"
    if any(output.endswith(".o") and "gtest_scale.dir" in output for output in outputs):
        return "gtest_tu_compile"
    if any(output.endswith(".o") and "doctest_scale.dir" in output for output in outputs):
        return "doctest_tu_compile"
    if any(output.endswith("gentest_scale") or output.endswith("gentest_scale.exe") for output in outputs):
        return "gentest_link"
    if any(output.endswith("gtest_scale") or output.endswith("gtest_scale.exe") for output in outputs):
        return "gtest_link"
    if any(output.endswith("doctest_scale") or output.endswith("doctest_scale.exe") for output in outputs):
        return "doctest_link"
    return "other"


def summarize_ninja_log(build_dir: Path, skip_lines: int = 0) -> dict[str, object]:
    ninja_log = build_dir / ".ninja_log"
    summary: dict[str, object] = {"wall_s": 0.0, "categories": {}, "top_edges": []}
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
    wall_ms = 0
    top_edges: list[dict[str, object]] = []
    for key, outputs in edges.items():
        start_ms, end_ms, _ = key
        wall_ms = max(wall_ms, end_ms)
        duration_s = (end_ms - start_ms) / 1000.0
        category = classify_outputs(outputs)
        item = categories.setdefault(category, {"count": 0, "summed_edge_s": 0.0, "max_edge_s": 0.0})
        item["count"] = int(item["count"]) + 1
        item["summed_edge_s"] = float(item["summed_edge_s"]) + duration_s
        item["max_edge_s"] = max(float(item["max_edge_s"]), duration_s)
        top_edges.append({"category": category, "duration_s": duration_s, "output": outputs[0]})

    summary["wall_s"] = wall_ms / 1000.0
    summary["categories"] = categories
    summary["top_edges"] = sorted(top_edges, key=lambda edge: float(edge["duration_s"]), reverse=True)[:10]
    return summary


def time_target(root: Path, build_dir: Path, target: str, jobs: int, env: dict[str, str], sample: int) -> dict[str, object]:
    clean_consumer_outputs(build_dir, target)
    ninja_log = build_dir / ".ninja_log"
    start_lines = ninja_log_line_count(ninja_log)
    log(f"[scale] Starting {target} sample {sample}")
    cmd = ["cmake", "--build", str(build_dir), "--target", target, "-j", str(jobs)]
    start = time.perf_counter()
    proc = subprocess.run(cmd, cwd=root, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    elapsed = time.perf_counter() - start
    if proc.returncode != 0:
        print(proc.stdout[-6000:], file=sys.stderr)
        raise subprocess.CalledProcessError(proc.returncode, cmd, output=proc.stdout)
    return {"elapsed_s": elapsed, "ninja_log": summarize_ninja_log(build_dir, start_lines)}


def median(values: list[float]) -> float:
    return statistics.median(values)


def median_category(profiles: list[dict[str, object]], category: str) -> float:
    values: list[float] = []
    for profile in profiles:
        categories = profile.get("categories", {})
        if not isinstance(categories, dict):
            values.append(0.0)
            continue
        data = categories.get(category, {})
        if not isinstance(data, dict):
            values.append(0.0)
            continue
        values.append(float(data.get("summed_edge_s", 0.0)))
    return median(values)


def linear_fit(points: list[tuple[float, float]]) -> dict[str, float]:
    n = len(points)
    if n < 2:
        return {"intercept_s": 0.0, "slope_s_per_case": 0.0}
    sx = sum(x for x, _ in points)
    sy = sum(y for _, y in points)
    sxx = sum(x * x for x, _ in points)
    sxy = sum(x * y for x, y in points)
    denom = n * sxx - sx * sx
    if denom == 0:
        return {"intercept_s": sy / n, "slope_s_per_case": 0.0}
    slope = (n * sxy - sx * sy) / denom
    intercept = (sy - slope * sx) / n
    return {"intercept_s": intercept, "slope_s_per_case": slope}


def parse_counts(raw: str) -> list[int]:
    counts = [int(item.strip()) for item in raw.split(",") if item.strip()]
    if not counts or any(count < 1 for count in counts):
        raise ValueError("--counts must contain positive integers")
    return counts


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--counts", default="1,10,50,100,250,500,1000", help="Comma-separated case counts")
    ap.add_argument("--samples", type=int, default=3)
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    ap.add_argument("--build-type", default="Release")
    ap.add_argument(
        "--codegen-build-type",
        default="Release",
        help="CMake build type for the prebuilt gentest_codegen host tool",
    )
    ap.add_argument("--output", default=None)
    ap.add_argument("--ccache", choices=["off", "on"], default="off")
    ap.add_argument("--skip-run", action="store_true")
    ap.add_argument("--doctest-repository", default=DEFAULT_DOCTEST_REPOSITORY, help="doctest Git repository used by the scale harness")
    ap.add_argument("--doctest-tag", default=DEFAULT_DOCTEST_TAG, help="doctest commit, tag, or branch used by the scale harness")
    args = ap.parse_args()

    if args.samples < 1:
        print("error: --samples must be at least 1", file=sys.stderr)
        return 2
    if args.jobs < 1:
        print("error: --jobs must be at least 1", file=sys.stderr)
        return 2

    counts = parse_counts(args.counts)
    root = repo_root()
    env = os.environ.copy()
    if args.ccache == "off":
        env["CCACHE_DISABLE"] = "1"

    build_root = root / "build" / "bench-case-scale"
    tool_build = build_root / "tool"
    source_root = build_root / "sources"
    output = Path(args.output) if args.output else build_root / "case_scale.json"
    if not output.is_absolute():
        output = root / output
    output.parent.mkdir(parents=True, exist_ok=True)

    sha = git(root, "rev-parse", "HEAD")
    codegen_exe = configure_and_build_codegen(root, tool_build, args.codegen_build_type, args.jobs, env)

    results: list[dict[str, object]] = []
    for count in counts:
        log(f"[scale] Preparing count={count}")
        source_dir = source_root / f"n_{count}"
        build_dir = build_root / f"build_n_{count}"
        write_scale_project(source_dir, root, count, args.doctest_repository, args.doctest_tag)
        run(
            [
                "cmake",
                "-S",
                str(source_dir),
                "-B",
                str(build_dir),
                *cmake_common_args(args.build_type),
                f"-DFETCHCONTENT_BASE_DIR={build_root / '_deps'}",
                f"-DGENTEST_CODEGEN_EXECUTABLE={codegen_exe}",
            ],
            cwd=root,
            env=env,
        )
        run(["cmake", "--build", str(build_dir), "--target", "gentest_main", "-j", str(args.jobs)], cwd=root, env=env)
        run(["cmake", "--build", str(build_dir), "--target", "doctest_scale_main", "-j", str(args.jobs)], cwd=root, env=env)

        for target in ("gentest_scale", "gtest_scale", "doctest_scale"):
            run(["cmake", "--build", str(build_dir), "--target", target, "-j", str(args.jobs)], cwd=root, env=env)
        if not args.skip_run:
            run([str(executable_path(build_dir, "gentest_scale"))], cwd=build_dir, env=env, capture=True)
            run([str(executable_path(build_dir, "gtest_scale"))], cwd=build_dir, env=env, capture=True)
            run([str(executable_path(build_dir, "doctest_scale"))], cwd=build_dir, env=env, capture=True)

        samples: dict[str, list[float]] = {"gentest_scale": [], "gtest_scale": [], "doctest_scale": []}
        profiles: dict[str, list[dict[str, object]]] = {"gentest_scale": [], "gtest_scale": [], "doctest_scale": []}
        for sample in range(1, args.samples + 1):
            for target in ("gentest_scale", "gtest_scale", "doctest_scale"):
                timed = time_target(root, build_dir, target, args.jobs, env, sample)
                samples[target].append(float(timed["elapsed_s"]))
                profile = timed["ninja_log"]
                assert isinstance(profile, dict)
                profiles[target].append(profile)

        gentest_profiles = profiles["gentest_scale"]
        gtest_profiles = profiles["gtest_scale"]
        doctest_profiles = profiles["doctest_scale"]
        entry = {
            "count": count,
            "gentest": {
                "samples_s": samples["gentest_scale"],
                "median_s": median(samples["gentest_scale"]),
                "codegen_s": median_category(gentest_profiles, "gentest_codegen"),
                "tu_compile_s": median_category(gentest_profiles, "gentest_tu_compile"),
                "link_s": median_category(gentest_profiles, "gentest_link"),
                "manifest_validation_s": median_category(gentest_profiles, "gentest_manifest_validation"),
            },
            "gtest": {
                "samples_s": samples["gtest_scale"],
                "median_s": median(samples["gtest_scale"]),
                "tu_compile_s": median_category(gtest_profiles, "gtest_tu_compile"),
                "link_s": median_category(gtest_profiles, "gtest_link"),
            },
            "doctest": {
                "samples_s": samples["doctest_scale"],
                "median_s": median(samples["doctest_scale"]),
                "tu_compile_s": median_category(doctest_profiles, "doctest_tu_compile"),
                "link_s": median_category(doctest_profiles, "doctest_link"),
            },
        }
        gentest = entry["gentest"]
        gtest = entry["gtest"]
        doctest = entry["doctest"]
        assert isinstance(gentest, dict)
        assert isinstance(gtest, dict)
        assert isinstance(doctest, dict)
        delta = float(gentest["median_s"]) - float(gtest["median_s"])
        doctest_delta = float(gentest["median_s"]) - float(doctest["median_s"])
        entry["delta_s"] = delta
        entry["delta_pct"] = (delta / float(gtest["median_s"]) * 100.0) if float(gtest["median_s"]) else 0.0
        entry["doctest_delta_s"] = doctest_delta
        entry["doctest_delta_pct"] = (doctest_delta / float(doctest["median_s"]) * 100.0) if float(doctest["median_s"]) else 0.0
        results.append(entry)

        print(
            f"[scale] n={count:<5} gentest={float(gentest['median_s']):.3f}s "
            f"(codegen={float(gentest['codegen_s']):.3f}s, tu={float(gentest['tu_compile_s']):.3f}s) "
            f"gtest={float(gtest['median_s']):.3f}s (tu={float(gtest['tu_compile_s']):.3f}s) "
            f"doctest={float(doctest['median_s']):.3f}s (tu={float(doctest['tu_compile_s']):.3f}s) "
            f"delta_gtest={float(entry['delta_s']):+.3f}s delta_doctest={float(entry['doctest_delta_s']):+.3f}s"
        )

    gentest_points = [(float(entry["count"]), float(entry["gentest"]["median_s"])) for entry in results]  # type: ignore[index]
    gtest_points = [(float(entry["count"]), float(entry["gtest"]["median_s"])) for entry in results]  # type: ignore[index]
    doctest_points = [(float(entry["count"]), float(entry["doctest"]["median_s"])) for entry in results]  # type: ignore[index]
    gentest_fit = linear_fit(gentest_points)
    gtest_fit = linear_fit(gtest_points)
    doctest_fit = linear_fit(doctest_points)
    slope_diff = gtest_fit["slope_s_per_case"] - gentest_fit["slope_s_per_case"]
    intercept_diff = gentest_fit["intercept_s"] - gtest_fit["intercept_s"]
    crossover_cases = None
    if slope_diff > 0:
        crossover_cases = intercept_diff / slope_diff
    doctest_slope_diff = doctest_fit["slope_s_per_case"] - gentest_fit["slope_s_per_case"]
    doctest_intercept_diff = gentest_fit["intercept_s"] - doctest_fit["intercept_s"]
    doctest_crossover_cases = None
    if doctest_slope_diff > 0:
        doctest_crossover_cases = doctest_intercept_diff / doctest_slope_diff

    result: dict[str, object] = {
        "sha": sha,
        "build_type": args.build_type,
        "codegen_build_type": args.codegen_build_type,
        "jobs": args.jobs,
        "samples": args.samples,
        "ccache": args.ccache,
        "doctest_repository": args.doctest_repository,
        "doctest_tag": args.doctest_tag,
        "counts": counts,
        "results": results,
        "fits": {
            "gentest": gentest_fit,
            "gtest": gtest_fit,
            "doctest": doctest_fit,
            "crossover_cases": crossover_cases,
            "doctest_crossover_cases": doctest_crossover_cases,
        },
    }
    output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    log(f"[scale] Wrote {output}")

    print("\n=== Scale Summary ===")
    print(f"Commit: {sha}")
    print(f"Counts: {', '.join(str(c) for c in counts)}")
    print(f"Samples: {args.samples}, jobs: {args.jobs}, build type: {args.build_type}, codegen build type: {args.codegen_build_type}")
    print()
    print(f"doctest: {args.doctest_repository} @ {args.doctest_tag}")
    print()
    print(f"{'N':>6} | {'gentest':>8} | {'codegen':>8} | {'gentest TU':>10} | {'gtest':>8} | {'doctest':>8} | {'d/gtest':>8} | {'d/doctest':>9}")
    print("-" * 98)
    for entry in results:
        gentest = entry["gentest"]
        gtest = entry["gtest"]
        doctest = entry["doctest"]
        assert isinstance(gentest, dict)
        assert isinstance(gtest, dict)
        assert isinstance(doctest, dict)
        print(
            f"{int(entry['count']):>6} | {float(gentest['median_s']):>8.3f} | {float(gentest['codegen_s']):>8.3f} | "
            f"{float(gentest['tu_compile_s']):>10.3f} | {float(gtest['median_s']):>8.3f} | "
            f"{float(doctest['median_s']):>8.3f} | {float(entry['delta_s']):>+8.3f} | {float(entry['doctest_delta_s']):>+9.3f}"
        )
    print()
    print(
        "Linear fit: "
        f"gentest={gentest_fit['intercept_s']:.3f}s + {gentest_fit['slope_s_per_case'] * 1000.0:.4f}ms/case*N; "
        f"gtest={gtest_fit['intercept_s']:.3f}s + {gtest_fit['slope_s_per_case'] * 1000.0:.4f}ms/case*N; "
        f"doctest={doctest_fit['intercept_s']:.3f}s + {doctest_fit['slope_s_per_case'] * 1000.0:.4f}ms/case*N"
    )
    if crossover_cases is None or crossover_cases < 0:
        print("No positive gentest-vs-GoogleTest crossover predicted by this fit.")
    else:
        print(f"Predicted gentest-vs-GoogleTest crossover: ~{crossover_cases:.0f} cases")
    if doctest_crossover_cases is None or doctest_crossover_cases < 0:
        print("No positive gentest-vs-doctest crossover predicted by this fit.")
    else:
        print(f"Predicted gentest-vs-doctest crossover: ~{doctest_crossover_cases:.0f} cases")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
