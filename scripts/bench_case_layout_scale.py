#!/usr/bin/env python3
"""
Scale synthetic tests across consumer layout shapes.

This script complements bench_case_scale.py by keeping total test count fixed
while distributing cases across either:

  - multi-tu: one target binary with multiple test translation units
  - multi-binary: multiple target binaries, each with one test translation unit

Timed scope matches bench_case_scale.py: configure, framework/runtime builds,
and the gentest_codegen tool build are excluded. gentest timing includes
codegen execution, generated TU compilation, and final binary links.
The prebuilt gentest_codegen host tool defaults to a Release build even when the
consumer target build type is Debug; override with --codegen-build-type when
you intentionally want to measure a differently built host tool.

Usage:
  python3 scripts/bench_case_layout_scale.py --layout multi-tu --shards 8
  python3 scripts/bench_case_layout_scale.py --layout multi-binary --shards 8
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

from bench_case_scale import (  # type: ignore[import-not-found]
    DEFAULT_DOCTEST_REPOSITORY,
    DEFAULT_DOCTEST_TAG,
    cmake_common_args,
    configure_and_build_codegen,
    git,
    linear_fit,
    median,
    median_category,
    parse_counts,
    repo_root,
    run,
)


FRAMEWORK_PREFIXES = {
    "gentest": "gentest_scale",
    "gtest": "gtest_scale",
    "doctest": "doctest_scale",
}


def log(message: str) -> None:
    print(message, flush=True)


def shard_ranges(count: int, requested_shards: int) -> list[range]:
    active = min(count, requested_shards)
    base = count // active
    remainder = count % active
    ranges: list[range] = []
    start = 0
    for shard in range(active):
        size = base + (1 if shard < remainder else 0)
        stop = start + size
        ranges.append(range(start, stop))
        start = stop
    return ranges


def quote_sources(names: list[str]) -> str:
    return "\n    ".join(f'"{name}"' for name in names)


def write_doctest_support(source_dir: Path) -> None:
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


def write_gentest_cases(path: Path, case_range: range, shard: int) -> None:
    lines = [
        '#include "gentest/attributes.h"',
        '#include "gentest/runner.h"',
        "using namespace gentest::asserts;",
        "",
        f"namespace scale::shard_{shard:03d} {{",
        "",
    ]
    for idx in case_range:
        lines.extend(
            [
                f'[[using gentest: test("case/{idx:06d}")]]',
                f"void case_{idx:06d}() {{",
                f"    EXPECT_EQ({idx}, {idx});",
                "}",
                "",
            ]
        )
    lines.append(f"}} // namespace scale::shard_{shard:03d}\n")
    path.write_text("\n".join(lines), encoding="utf-8")


def write_gtest_cases(path: Path, case_range: range, shard: int) -> None:
    lines = [
        "#include <gtest/gtest.h>",
        "",
        f"namespace scale::shard_{shard:03d} {{",
        "",
    ]
    for idx in case_range:
        lines.extend(
            [
                f"TEST(ScaleShard{shard:03d}, Case{idx:06d}) {{",
                f"    EXPECT_EQ({idx}, {idx});",
                "}",
                "",
            ]
        )
    lines.append(f"}} // namespace scale::shard_{shard:03d}\n")
    path.write_text("\n".join(lines), encoding="utf-8")


def write_doctest_cases(path: Path, case_range: range, shard: int) -> None:
    lines = [
        '#include "doctest_compat.hpp"',
        "",
        f"namespace scale::shard_{shard:03d} {{",
        "",
    ]
    for idx in case_range:
        lines.extend(
            [
                f'TEST_CASE("case/{idx:06d}") {{',
                f"    CHECK_EQ({idx}, {idx});",
                "}",
                "",
            ]
        )
    lines.append(f"}} // namespace scale::shard_{shard:03d}\n")
    path.write_text("\n".join(lines), encoding="utf-8")


def write_case_sources(source_dir: Path, ranges: list[range]) -> dict[str, list[str]]:
    sources: dict[str, list[str]] = {"gentest": [], "gtest": [], "doctest": []}
    for shard, case_range in enumerate(ranges):
        gentest_name = f"gentest_cases_{shard:03d}.cpp"
        gtest_name = f"gtest_cases_{shard:03d}.cpp"
        doctest_name = f"doctest_cases_{shard:03d}.cpp"
        write_gentest_cases(source_dir / gentest_name, case_range, shard)
        write_gtest_cases(source_dir / gtest_name, case_range, shard)
        write_doctest_cases(source_dir / doctest_name, case_range, shard)
        sources["gentest"].append(gentest_name)
        sources["gtest"].append(gtest_name)
        sources["doctest"].append(doctest_name)
    return sources


def cmake_preamble(root: Path, count: int, layout: str, doctest_repository: str, doctest_tag: str) -> str:
    project_suffix = layout.replace("-", "_")
    return f"""cmake_minimum_required(VERSION 3.24)
project(gentest_gtest_{project_suffix}_scale_{count} LANGUAGES C CXX)

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

add_library(doctest_scale_main STATIC doctest_main.cpp)
target_link_libraries(doctest_scale_main PUBLIC doctest::doctest)
target_compile_features(doctest_scale_main PUBLIC cxx_std_20)

"""


def write_multi_tu_cmake(source_dir: Path, root: Path, sources: dict[str, list[str]], preamble: str) -> None:
    (source_dir / "CMakeLists.txt").write_text(
        preamble
        + f"""set(GENTEST_SCALE_SOURCES
    {quote_sources(sources["gentest"])})
set(GTEST_SCALE_SOURCES
    {quote_sources(sources["gtest"])})
set(DOCTEST_SCALE_SOURCES
    {quote_sources(sources["doctest"])})

add_executable(gentest_scale ${{GENTEST_SCALE_SOURCES}})
target_link_libraries(gentest_scale PRIVATE gentest_main)
target_compile_features(gentest_scale PRIVATE cxx_std_20)
target_include_directories(gentest_scale PRIVATE "{root}/include")
gentest_attach_codegen(gentest_scale
    OUTPUT_DIR "${{CMAKE_CURRENT_BINARY_DIR}}/gentest_generated"
    CLANG_ARGS
        -std=c++20
        "-I{root}/include")

add_executable(gtest_scale ${{GTEST_SCALE_SOURCES}})
target_link_libraries(gtest_scale PRIVATE PkgConfig::GTEST)
target_compile_features(gtest_scale PRIVATE cxx_std_20)

add_executable(doctest_scale ${{DOCTEST_SCALE_SOURCES}})
target_link_libraries(doctest_scale PRIVATE doctest_scale_main)
target_compile_features(doctest_scale PRIVATE cxx_std_20)
""",
        encoding="utf-8",
    )


def write_multi_binary_cmake(source_dir: Path, root: Path, sources: dict[str, list[str]], preamble: str) -> None:
    chunks = [preamble]
    gentest_targets: list[str] = []
    gtest_targets: list[str] = []
    doctest_targets: list[str] = []

    for shard, gentest_source in enumerate(sources["gentest"]):
        gtest_source = sources["gtest"][shard]
        doctest_source = sources["doctest"][shard]
        gentest_target = f"gentest_scale_{shard:03d}"
        gtest_target = f"gtest_scale_{shard:03d}"
        doctest_target = f"doctest_scale_{shard:03d}"
        gentest_targets.append(gentest_target)
        gtest_targets.append(gtest_target)
        doctest_targets.append(doctest_target)
        chunks.append(
            f"""
add_executable({gentest_target} "{gentest_source}")
target_link_libraries({gentest_target} PRIVATE gentest_main)
target_compile_features({gentest_target} PRIVATE cxx_std_20)
target_include_directories({gentest_target} PRIVATE "{root}/include")
gentest_attach_codegen({gentest_target}
    OUTPUT_DIR "${{CMAKE_CURRENT_BINARY_DIR}}/gentest_generated_{shard:03d}"
    CLANG_ARGS
        -std=c++20
        "-I{root}/include")

add_executable({gtest_target} "{gtest_source}")
target_link_libraries({gtest_target} PRIVATE PkgConfig::GTEST)
target_compile_features({gtest_target} PRIVATE cxx_std_20)

add_executable({doctest_target} "{doctest_source}")
target_link_libraries({doctest_target} PRIVATE doctest_scale_main)
target_compile_features({doctest_target} PRIVATE cxx_std_20)
"""
        )

    chunks.extend(
        [
            "\nadd_custom_target(gentest_scale_all DEPENDS " + " ".join(gentest_targets) + ")\n",
            "add_custom_target(gtest_scale_all DEPENDS " + " ".join(gtest_targets) + ")\n",
            "add_custom_target(doctest_scale_all DEPENDS " + " ".join(doctest_targets) + ")\n",
        ]
    )
    (source_dir / "CMakeLists.txt").write_text("".join(chunks), encoding="utf-8")


def write_scale_project(
    source_dir: Path,
    root: Path,
    count: int,
    layout: str,
    shards: int,
    doctest_repository: str,
    doctest_tag: str,
) -> int:
    source_dir.mkdir(parents=True, exist_ok=True)
    ranges = shard_ranges(count, shards)
    write_doctest_support(source_dir)
    sources = write_case_sources(source_dir, ranges)
    preamble = cmake_preamble(root, count, layout, doctest_repository, doctest_tag)
    if layout == "multi-tu":
        write_multi_tu_cmake(source_dir, root, sources, preamble)
    elif layout == "multi-binary":
        write_multi_binary_cmake(source_dir, root, sources, preamble)
    else:
        raise ValueError(f"unsupported layout: {layout}")
    return len(ranges)


def executable_path(build_dir: Path, target: str) -> Path:
    suffix = ".exe" if os.name == "nt" else ""
    return build_dir / f"{target}{suffix}"


def framework_targets(framework: str, layout: str, active_shards: int) -> list[str]:
    prefix = FRAMEWORK_PREFIXES[framework]
    if layout == "multi-binary":
        return [f"{prefix}_{shard:03d}" for shard in range(active_shards)]
    return [prefix]


def build_target(framework: str, layout: str) -> str:
    prefix = FRAMEWORK_PREFIXES[framework]
    if layout == "multi-binary":
        return f"{prefix}_all"
    return prefix


def clean_framework_outputs(build_dir: Path, framework: str, layout: str, active_shards: int) -> None:
    targets = framework_targets(framework, layout, active_shards)
    aggregate = build_target(framework, layout)
    for target in [*targets, aggregate]:
        for path in (executable_path(build_dir, target), build_dir / "CMakeFiles" / f"{target}.dir"):
            if path.is_dir():
                shutil.rmtree(path)
            elif path.exists():
                path.unlink()

    if framework != "gentest":
        return

    generated_dirs = [build_dir / "gentest_generated"]
    generated_dirs.extend(build_dir / f"gentest_generated_{shard:03d}" for shard in range(active_shards))
    for generated_dir in generated_dirs:
        if not generated_dir.exists():
            continue
        for pattern in ("*.gentest.h", "*.artifact_manifest.json", "*.artifact_manifest.validated"):
            for path in generated_dir.glob(pattern):
                path.unlink()


def ninja_log_line_count(ninja_log: Path) -> int:
    if not ninja_log.exists():
        return 0
    with ninja_log.open("r", encoding="utf-8") as f:
        return sum(1 for _ in f)


def output_basename(output: str) -> str:
    return Path(output).name


def output_is_link(output: str, prefix: str) -> bool:
    name = output_basename(output)
    if os.name == "nt" and name.endswith(".exe"):
        name = name[:-4]
    return name == prefix or name.startswith(f"{prefix}_")


def classify_outputs(outputs: list[str]) -> str:
    if any(output.endswith(".artifact_manifest.validated") for output in outputs):
        return "gentest_manifest_validation"
    if any(output.endswith(".gentest.h") or output.endswith(".gentest.cpp") for output in outputs):
        return "gentest_codegen"
    if any(output.endswith(".o") and "gentest_scale" in output for output in outputs):
        return "gentest_tu_compile"
    if any(output.endswith(".o") and "gtest_scale" in output for output in outputs):
        return "gtest_tu_compile"
    if any(output.endswith(".o") and "doctest_scale" in output for output in outputs):
        return "doctest_tu_compile"
    if any(output_is_link(output, "gentest_scale") for output in outputs):
        return "gentest_link"
    if any(output_is_link(output, "gtest_scale") for output in outputs):
        return "gtest_link"
    if any(output_is_link(output, "doctest_scale") for output in outputs):
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


def time_framework(
    root: Path,
    build_dir: Path,
    framework: str,
    layout: str,
    active_shards: int,
    jobs: int,
    env: dict[str, str],
    sample: int,
) -> dict[str, object]:
    clean_framework_outputs(build_dir, framework, layout, active_shards)
    ninja_log = build_dir / ".ninja_log"
    start_lines = ninja_log_line_count(ninja_log)
    target = build_target(framework, layout)
    log(f"[layout-scale] Starting {target} sample {sample}")
    cmd = ["cmake", "--build", str(build_dir), "--target", target, "-j", str(jobs)]
    start = time.perf_counter()
    proc = subprocess.run(cmd, cwd=root, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    elapsed = time.perf_counter() - start
    if proc.returncode != 0:
        print(proc.stdout[-6000:], file=sys.stderr)
        raise subprocess.CalledProcessError(proc.returncode, cmd, output=proc.stdout)
    return {"elapsed_s": elapsed, "ninja_log": summarize_ninja_log(build_dir, start_lines)}


def run_framework_binaries(build_dir: Path, framework: str, layout: str, active_shards: int, env: dict[str, str]) -> None:
    for target in framework_targets(framework, layout, active_shards):
        run([str(executable_path(build_dir, target))], cwd=build_dir, env=env, capture=True)


def default_output(root: Path, layout: str, shards: int) -> Path:
    return root / "build" / "bench-case-layout-scale" / f"{layout}-shards-{shards}" / f"case_scale_{layout}_shards_{shards}.json"


def print_summary(result: dict[str, object]) -> None:
    results = result["results"]
    fits = result["fits"]
    assert isinstance(results, list)
    assert isinstance(fits, dict)
    gentest_fit = fits["gentest"]
    gtest_fit = fits["gtest"]
    doctest_fit = fits["doctest"]
    assert isinstance(gentest_fit, dict)
    assert isinstance(gtest_fit, dict)
    assert isinstance(doctest_fit, dict)

    print("\n=== Layout Scale Summary ===")
    print(f"Commit: {result['sha']}")
    print(f"Layout: {result['layout']}, requested shards: {result['shards']}")
    print(f"Counts: {', '.join(str(c) for c in result['counts'])}")
    print(
        f"Samples: {result['samples']}, jobs: {result['jobs']}, build type: {result['build_type']}, "
        f"codegen build type: {result['codegen_build_type']}"
    )
    print()
    print(f"doctest: {result['doctest_repository']} @ {result['doctest_tag']}")
    print()
    print(
        f"{'N':>6} | {'shards':>6} | {'gentest':>8} | {'codegen':>8} | {'gentest TU':>10} | "
        f"{'gtest':>8} | {'doctest':>8} | {'d/gtest':>8} | {'d/doctest':>9}"
    )
    print("-" * 107)
    for entry in results:
        assert isinstance(entry, dict)
        gentest = entry["gentest"]
        gtest = entry["gtest"]
        doctest = entry["doctest"]
        assert isinstance(gentest, dict)
        assert isinstance(gtest, dict)
        assert isinstance(doctest, dict)
        print(
            f"{int(entry['count']):>6} | {int(entry['active_shards']):>6} | "
            f"{float(gentest['median_s']):>8.3f} | {float(gentest['codegen_s']):>8.3f} | "
            f"{float(gentest['tu_compile_s']):>10.3f} | {float(gtest['median_s']):>8.3f} | "
            f"{float(doctest['median_s']):>8.3f} | {float(entry['delta_s']):>+8.3f} | "
            f"{float(entry['doctest_delta_s']):>+9.3f}"
        )
    print()
    print(
        "Linear fit: "
        f"gentest={float(gentest_fit['intercept_s']):.3f}s + {float(gentest_fit['slope_s_per_case']) * 1000.0:.4f}ms/case*N; "
        f"gtest={float(gtest_fit['intercept_s']):.3f}s + {float(gtest_fit['slope_s_per_case']) * 1000.0:.4f}ms/case*N; "
        f"doctest={float(doctest_fit['intercept_s']):.3f}s + {float(doctest_fit['slope_s_per_case']) * 1000.0:.4f}ms/case*N"
    )
    crossover = fits.get("crossover_cases")
    doctest_crossover = fits.get("doctest_crossover_cases")
    if isinstance(crossover, (float, int)) and crossover >= 0:
        print(f"Predicted gentest-vs-GoogleTest crossover: ~{crossover:.0f} cases")
    else:
        print("No positive gentest-vs-GoogleTest crossover predicted by this fit.")
    if isinstance(doctest_crossover, (float, int)) and doctest_crossover >= 0:
        print(f"Predicted gentest-vs-doctest crossover: ~{doctest_crossover:.0f} cases")
    else:
        print("No positive gentest-vs-doctest crossover predicted by this fit.")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--layout", choices=["multi-tu", "multi-binary"], required=True)
    ap.add_argument("--shards", type=int, default=8, help="Maximum test TUs or binaries to split each count over")
    ap.add_argument("--counts", default="1,10,50,100,250,500,1000", help="Comma-separated total case counts")
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
    if args.shards < 1:
        print("error: --shards must be at least 1", file=sys.stderr)
        return 2

    counts = parse_counts(args.counts)
    root = repo_root()
    env = os.environ.copy()
    if args.ccache == "off":
        env["CCACHE_DISABLE"] = "1"

    build_root = root / "build" / "bench-case-layout-scale" / f"{args.layout}-shards-{args.shards}"
    tool_build = build_root / "tool"
    source_root = build_root / "sources"
    output = Path(args.output) if args.output else default_output(root, args.layout, args.shards)
    if not output.is_absolute():
        output = root / output
    output.parent.mkdir(parents=True, exist_ok=True)

    sha = git(root, "rev-parse", "HEAD")
    codegen_exe = configure_and_build_codegen(root, tool_build, args.codegen_build_type, args.jobs, env)

    results: list[dict[str, object]] = []
    for count in counts:
        log(f"[layout-scale] Preparing layout={args.layout} count={count}")
        source_dir = source_root / f"n_{count}"
        build_dir = build_root / f"build_n_{count}"
        active_shards = write_scale_project(source_dir, root, count, args.layout, args.shards, args.doctest_repository, args.doctest_tag)
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

        for framework in ("gentest", "gtest", "doctest"):
            run(["cmake", "--build", str(build_dir), "--target", build_target(framework, args.layout), "-j", str(args.jobs)], cwd=root, env=env)
        if not args.skip_run:
            for framework in ("gentest", "gtest", "doctest"):
                run_framework_binaries(build_dir, framework, args.layout, active_shards, env)

        samples: dict[str, list[float]] = {"gentest": [], "gtest": [], "doctest": []}
        profiles: dict[str, list[dict[str, object]]] = {"gentest": [], "gtest": [], "doctest": []}
        for sample in range(1, args.samples + 1):
            for framework in ("gentest", "gtest", "doctest"):
                timed = time_framework(root, build_dir, framework, args.layout, active_shards, args.jobs, env, sample)
                samples[framework].append(float(timed["elapsed_s"]))
                profile = timed["ninja_log"]
                assert isinstance(profile, dict)
                profiles[framework].append(profile)

        gentest_profiles = profiles["gentest"]
        gtest_profiles = profiles["gtest"]
        doctest_profiles = profiles["doctest"]
        entry = {
            "count": count,
            "layout": args.layout,
            "requested_shards": args.shards,
            "active_shards": active_shards,
            "cases_per_shard": [len(item) for item in shard_ranges(count, args.shards)],
            "gentest": {
                "samples_s": samples["gentest"],
                "median_s": median(samples["gentest"]),
                "codegen_s": median_category(gentest_profiles, "gentest_codegen"),
                "tu_compile_s": median_category(gentest_profiles, "gentest_tu_compile"),
                "link_s": median_category(gentest_profiles, "gentest_link"),
                "manifest_validation_s": median_category(gentest_profiles, "gentest_manifest_validation"),
            },
            "gtest": {
                "samples_s": samples["gtest"],
                "median_s": median(samples["gtest"]),
                "tu_compile_s": median_category(gtest_profiles, "gtest_tu_compile"),
                "link_s": median_category(gtest_profiles, "gtest_link"),
            },
            "doctest": {
                "samples_s": samples["doctest"],
                "median_s": median(samples["doctest"]),
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
            f"[layout-scale] layout={args.layout} n={count:<5} shards={active_shards:<2} "
            f"gentest={float(gentest['median_s']):.3f}s "
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
        "layout": args.layout,
        "shards": args.shards,
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
    log(f"[layout-scale] Wrote {output}")
    print_summary(result)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
