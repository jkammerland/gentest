#!/usr/bin/env python3
"""Reproducible Gentest compile-time benchmark campaign.

The campaign deliberately isolates generated consumer fixtures and build
trees. Repository E2E builds use a campaign-unique directory beneath their
source tree so CMake's absolute dependency handling stays valid without
reusing prior state. It measures seven raw samples (after two warmups) by
default, records median plus median absolute deviation (MAD), and alternates
codegen-cap order for every sample round.

Examples:
  python3 scripts/bench_compile_campaign.py --cxx clang++-22 --cc clang-22
  python3 scripts/bench_compile_campaign.py --cxx g++-16 --cc gcc-16 \\
      --lanes one-tu,eight-tu-one-binary,eight-tu-eight-binary
  python3 scripts/bench_compile_campaign.py --cxx clang++-22 --allow-unavailable

This is a measurement harness, not a performance gate.  It writes ``result.json``
and ``summary.md`` and never invents results for an unavailable compiler.
"""
from __future__ import annotations

import argparse
import collections
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Callable

from compile_bench_common import alternating_order, codegen_job_values, median_mad, parse_codegen_jobs


BASELINE_COMMIT = "9edd3c826eadb31714f6462b5264cc1793bb535b"
DEFAULT_LANES = ("one-tu", "eight-tu-one-binary", "eight-tu-eight-binary", "repo-e2e", "runtime")
DEFAULT_SCENARIOS = (
    "cold-build",
    "no-op",
    "reconfigure",
    "source-edit",
    "private-header-edit",
    "shared-header-edit",
    "equivalent-compdb-rewrite",
    "unrelated-compdb-rewrite",
)
REPO_SCENARIOS = ("cold-build", "no-op", "reconfigure", "equivalent-compdb-rewrite", "unrelated-compdb-rewrite")
RUNTIME_SCENARIOS = ("cold-build", "no-op", "reconfigure")
REPO_TARGETS = ("gentest_unit_tests", "gentest_fixtures_tests", "gentest_mocking_tests")


def log(message: str) -> None:
    print(message, flush=True)


def run(command: list[str], *, cwd: Path, env: dict[str, str], capture: bool = False) -> subprocess.CompletedProcess[str]:
    kwargs: dict[str, object] = {"cwd": cwd, "env": env, "text": True}
    if capture:
        kwargs.update({"stdout": subprocess.PIPE, "stderr": subprocess.STDOUT})
    completed = subprocess.run(command, **kwargs)
    if completed.returncode:
        if capture and completed.stdout:
            print(completed.stdout[-8000:], file=sys.stderr)
        raise subprocess.CalledProcessError(completed.returncode, command, output=completed.stdout)
    return completed


def run_output(command: list[str], *, cwd: Path | None = None, env: dict[str, str] | None = None) -> str:
    completed = subprocess.run(command, cwd=cwd, env=env, check=True, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return completed.stdout.strip()


def git(root: Path, *args: str) -> str:
    return run_output(["git", *args], cwd=root)


def checkout_state(root: Path) -> dict[str, object]:
    full_status = git(root, "status", "--porcelain")
    tracked_status = git(root, "status", "--porcelain", "--untracked-files=no")
    untracked = git(root, "ls-files", "--others", "--exclude-standard")
    return {
        "dirty": bool(full_status),
        "status_porcelain": full_status.splitlines(),
        "tracked_dirty": bool(tracked_status),
        "tracked_status_porcelain": tracked_status.splitlines(),
        "untracked_files": untracked.splitlines(),
    }


def version(identity: str | Path | None) -> dict[str, object]:
    if identity is None:
        return {"path": None, "version": None}
    resolved = shutil.which(str(identity)) or str(identity)
    binary = Path(resolved)
    info: dict[str, object] = {"path": str(binary)}
    if binary.exists() and binary.is_file():
        info["sha256"] = hashlib.sha256(binary.read_bytes()).hexdigest()
    info["version"] = run_output([str(resolved), "--version"]) if shutil.which(str(resolved)) or binary.exists() else None
    return info


def machine_provenance(root: Path, cc: str, cxx: str, generator: str, build_type: str, cache: str) -> dict[str, object]:
    cpu_model = None
    memory_bytes = None
    cpuinfo = Path("/proc/cpuinfo")
    meminfo = Path("/proc/meminfo")
    if cpuinfo.exists():
        for line in cpuinfo.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.lower().startswith("model name"):
                cpu_model = line.split(":", 1)[1].strip()
                break
    if meminfo.exists():
        match = re.search(r"^MemTotal:\s+(\d+)\s+kB", meminfo.read_text(encoding="utf-8", errors="replace"), re.MULTILINE)
        if match:
            memory_bytes = int(match.group(1)) * 1024
    checkout = checkout_state(root)
    return {
        "machine": {
            "platform": platform.platform(),
            "system": platform.system(),
            "release": platform.release(),
            "machine": platform.machine(),
            "python": sys.version,
            "cpu_model": cpu_model,
            "logical_cores": os.cpu_count(),
            "memory_bytes": memory_bytes,
        },
        "git": {
            "head": git(root, "rev-parse", "HEAD"),
            "baseline_commit": BASELINE_COMMIT,
            "baseline_is_ancestor": subprocess.run(["git", "merge-base", "--is-ancestor", BASELINE_COMMIT, "HEAD"], cwd=root).returncode == 0,
            **checkout,
        },
        "tools": {
            "cmake": version("cmake"),
            "ninja": version("ninja"),
            "cc": version(cc),
            "cxx": version(cxx),
        },
        "build": {"generator": generator, "build_type": build_type, "cache_mode": cache},
        "environment": {key: os.environ.get(key) for key in sorted(os.environ) if key.startswith(("GENTEST_", "CCACHE_", "SCCACHE_", "CMAKE_"))},
    }


def resolve_cache_tool(mode: str, lookup: Callable[[str], str | None] = shutil.which) -> str | None:
    if mode == "off":
        return None
    tool = lookup(mode)
    if not tool:
        raise RuntimeError(f"compiler cache mode '{mode}' was requested, but '{mode}' is not on PATH")
    return tool


def require_clean_checkout(dirty: bool, allow_dirty: bool) -> None:
    if dirty and not allow_dirty:
        raise RuntimeError("checkout is dirty; use --allow-dirty to measure it explicitly")


def require_fresh_output(path: Path) -> None:
    if path.exists() and any(path.iterdir()):
        raise RuntimeError(f"output directory is not empty; choose a fresh directory: {path}")


def stop_sccache_server(env: dict[str, str], root: Path, tool: str | None = None, *, check: bool) -> None:
    resolved = tool or shutil.which("sccache")
    if not resolved:
        return
    if check:
        run_output([resolved, "--stop-server"], cwd=root, env=env)
        return
    subprocess.run(
        [resolved, "--stop-server"],
        cwd=root,
        env=env,
        text=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )


def cleanup_sccache_endpoint(metadata: dict[str, object]) -> None:
    temporary_directory = metadata.pop("_server_temporary_directory", None)
    if isinstance(temporary_directory, str):
        shutil.rmtree(temporary_directory, ignore_errors=True)


def shutdown_sccache(
    env: dict[str, str], root: Path, metadata: dict[str, object], tool: str | None = None, *, check: bool
) -> None:
    try:
        stop_sccache_server(env, root, tool, check=check)
    finally:
        cleanup_sccache_endpoint(metadata)


def cache_environment(mode: str, root: Path) -> tuple[dict[str, str], dict[str, object]]:
    env = os.environ.copy()
    env.pop("CMAKE_C_COMPILER_LAUNCHER", None)
    env.pop("CMAKE_CXX_COMPILER_LAUNCHER", None)
    cache_dir = root / "compiler-cache"
    metadata: dict[str, object] = {"mode": mode, "directory": str(cache_dir), "before": None, "after": None}
    if mode == "off":
        env["CCACHE_DISABLE"] = "1"
        env["SCCACHE_DISABLE"] = "1"
        return env, metadata
    tool = resolve_cache_tool(mode)
    assert tool is not None
    cache_dir.mkdir(parents=True, exist_ok=True)
    if mode == "ccache":
        env.pop("CCACHE_DISABLE", None)
        env["SCCACHE_DISABLE"] = "1"
        env["CCACHE_DIR"] = str(cache_dir)
        # Do not read or change a user's global cache settings.
        env["CCACHE_CONFIGPATH"] = os.devnull
        metadata["before"] = run_output([tool, "--show-stats"], cwd=root, env=env)
    else:
        env.pop("SCCACHE_DISABLE", None)
        env["CCACHE_DISABLE"] = "1"
        env["SCCACHE_DIR"] = str(cache_dir)
        env.pop("SCCACHE_SERVER_PORT", None)
        env.pop("SCCACHE_SERVER_UDS", None)
        env.pop("SCCACHE_CONF", None)
        config_path = cache_dir / "sccache.conf"
        config_path.write_text("", encoding="utf-8")
        env["SCCACHE_CONF"] = str(config_path)
        if os.name == "nt":
            raise RuntimeError("isolated sccache campaigns are not yet supported on Windows")
        socket_path = cache_dir / "server.sock"
        if len(os.fsencode(socket_path)) >= 100:
            temporary_directory = tempfile.mkdtemp(prefix="gentest-sccache-")
            metadata["_server_temporary_directory"] = temporary_directory
            socket_path = Path(temporary_directory) / "server.sock"
        env["SCCACHE_SERVER_UDS"] = str(socket_path)
        metadata["server_endpoint"] = {"kind": "uds", "path": str(socket_path)}
        try:
            run_output([tool, "--start-server"], cwd=root, env=env)
            metadata["before"] = run_output([tool, "--show-stats"], cwd=root, env=env)
        except (OSError, subprocess.CalledProcessError):
            shutdown_sccache(env, root, metadata, tool, check=False)
            raise
    metadata["tool"] = version(tool)
    return env, metadata


def finish_cache_metadata(mode: str, env: dict[str, str], root: Path, metadata: dict[str, object]) -> None:
    if mode == "off":
        return
    tool = shutil.which(mode)
    if tool:
        try:
            metadata["after"] = run_output([tool, "--show-stats"], cwd=root, env=env)
        finally:
            if mode == "sccache":
                shutdown_sccache(env, root, metadata, tool, check=True)


def cmake_arguments(cc: str, cxx: str, build_type: str, cache: str) -> list[str]:
    arguments = [
        "-G",
        "Ninja",
        f"-DCMAKE_BUILD_TYPE={build_type}",
        f"-DCMAKE_C_COMPILER={cc}",
        f"-DCMAKE_CXX_COMPILER={cxx}",
        "-DCMAKE_TOOLCHAIN_FILE=",
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        "-DCMAKE_CXX_EXTENSIONS=OFF",
    ]
    if cache == "off":
        arguments.extend(["-DCMAKE_C_COMPILER_LAUNCHER=", "-DCMAKE_CXX_COMPILER_LAUNCHER="])
    else:
        arguments.extend([f"-DCMAKE_C_COMPILER_LAUNCHER={cache}", f"-DCMAKE_CXX_COMPILER_LAUNCHER={cache}"])
    for variable in ("LLVM_DIR", "Clang_DIR"):
        if os.environ.get(variable):
            arguments.append(f"-D{variable}={os.environ[variable]}")
    return arguments


def codegen_executable(build_dir: Path) -> Path:
    candidate = build_dir / "tools" / ("gentest_codegen.exe" if os.name == "nt" else "gentest_codegen")
    if not candidate.exists():
        raise RuntimeError(f"host gentest_codegen was not produced at {candidate}")
    return candidate


def fresh_repository_build_root(source: Path) -> Path:
    parent = source / "build"
    parent.mkdir(parents=True, exist_ok=True)
    return Path(tempfile.mkdtemp(prefix="compile-campaign-", dir=parent))


def configure_host_codegen(source: Path, output: Path, cc: str, cxx: str, jobs: int, cache: str, env: dict[str, str]) -> Path:
    # Deliberately Release even when the consumer fixture is Debug.
    build = output / "host-codegen-release"
    run(
        [
            "cmake", "-S", str(source), "-B", str(build), *cmake_arguments(cc, cxx, "Release", cache),
            "-Dgentest_BUILD_TESTING=OFF", "-DGENTEST_BUILD_CODEGEN=ON",
        ],
        cwd=source,
        env=env,
        capture=True,
    )
    run(["cmake", "--build", str(build), "--target", "gentest_codegen", "-j", str(jobs)], cwd=source, env=env, capture=True)
    return codegen_executable(build)


def write_fixture(source: Path, root: Path) -> dict[str, list[str]]:
    source.mkdir(parents=True, exist_ok=True)
    (source / "private.hpp").write_text("#pragma once\ninline constexpr int campaign_private = 7;\n", encoding="utf-8")
    (source / "shared.hpp").write_text("#pragma once\ninline constexpr int campaign_shared = 11;\n", encoding="utf-8")
    for index in range(8):
        includes = '#include "shared.hpp"\n'
        if index == 0:
            includes += '#include "private.hpp"\n'
        body = "campaign_shared"
        if index == 0:
            body += " + campaign_private"
        (source / f"case_{index:02d}.cpp").write_text(
            "#include <gentest/attributes.h>\n"
            f"{includes}\n"
            f"[[using gentest: test(\"campaign/case/{index:02d}\")]]\n"
            f"void campaign_case_{index:02d}() {{ (void)({body}); }}\n",
            encoding="utf-8",
        )
    cases = " ".join(f"case_{index:02d}.cpp" for index in range(8))
    binaries = "\n".join(
        f"add_campaign(campaign_binary_{index:02d} case_{index:02d}.cpp)" for index in range(8)
    )
    binary_names = " ".join(f"campaign_binary_{index:02d}" for index in range(8))
    (source / "CMakeLists.txt").write_text(
        f"""cmake_minimum_required(VERSION 3.24)
project(gentest_compile_campaign LANGUAGES C CXX)
set(gentest_BUILD_TESTING OFF CACHE BOOL \"\" FORCE)
set(GENTEST_BUILD_CODEGEN OFF CACHE BOOL \"\" FORCE)
add_subdirectory(\"{root.as_posix()}\" gentest EXCLUDE_FROM_ALL)
function(add_campaign name)
  add_executable(${{name}} ${{ARGN}})
  target_compile_features(${{name}} PRIVATE cxx_std_20)
  target_link_libraries(${{name}} PRIVATE gentest::gentest_main)
  gentest_attach_codegen(${{name}} OUTPUT_DIR \"${{CMAKE_CURRENT_BINARY_DIR}}/generated/${{name}}\")
endfunction()
add_campaign(campaign_one_tu case_00.cpp)
add_campaign(campaign_eight_tu {cases})
{binaries}
add_custom_target(campaign_eight_binary DEPENDS {binary_names})
""",
        encoding="utf-8",
    )
    return {
        "one-tu": ["campaign_one_tu"],
        "eight-tu-one-binary": ["campaign_eight_tu"],
        "eight-tu-eight-binary": ["campaign_eight_binary"],
    }


NinjaLogRecord = tuple[str, str, str, str, str]


def ninja_log_snapshot(build: Path) -> collections.Counter[NinjaLogRecord]:
    path = build / ".ninja_log"
    records: collections.Counter[NinjaLogRecord] = collections.Counter()
    if not path.exists():
        return records
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        fields = line.split("\t")
        if len(fields) < 5 or line.startswith("#"):
            continue
        records[(fields[3], fields[0], fields[1], fields[2], fields[4])] += 1
    return records


def recompact_ninja_log(source: Path, build: Path, env: dict[str, str]) -> None:
    # Ninja may compact its log when the next build opens it. Compact before
    # taking the snapshot so every record appended by the measured invocation
    # remains observable, including an identical repeated edge whose changed-
    # only output kept the same mtime.
    run(["ninja", "-C", str(build), "-t", "recompact"], cwd=source, env=env, capture=True)


def classify(outputs: list[str], executable_targets: tuple[str, ...] = ()) -> str:
    if any(".gentest.h" in output or "_mock_" in output for output in outputs):
        return "codegen"
    if any(output.endswith(".gentest.cpp.o") or output.endswith(".gentest.cpp.obj") for output in outputs):
        return "generated_tu_compile"
    if any(output.endswith((".o", ".obj")) for output in outputs):
        return "compile"
    target_names = {Path(target).name.removesuffix(".exe") for target in executable_targets}
    if any(
        output.endswith((".a", ".lib", ".exe"))
        or Path(output).name.removesuffix(".exe") in target_names
        for output in outputs
    ):
        return "link_or_archive"
    return "other"


def summarize_new_ninja_edges(
    build: Path,
    before: collections.Counter[NinjaLogRecord],
    executable_targets: tuple[str, ...] = (),
) -> dict[str, object]:
    categories: dict[str, int] = {}
    edges: dict[tuple[str, str, str, int], list[str]] = {}
    new_records = ninja_log_snapshot(build)
    new_records.subtract(before)
    for (output, start, end, _mtime, command_hash), count in new_records.items():
        for occurrence in range(max(0, count)):
            key = (start, end, command_hash, occurrence)
            edges.setdefault(key, []).append(output)
    for outputs in edges.values():
        category = classify(outputs, executable_targets)
        categories[category] = categories.get(category, 0) + 1
    return {"unique_edges": len(edges), "categories": categories}


def build_target(source: Path, build: Path, targets: list[str], jobs: int, env: dict[str, str]) -> tuple[float, dict[str, object]]:
    recompact_ninja_log(source, build, env)
    before = ninja_log_snapshot(build)
    command = ["cmake", "--build", str(build), "--target", *targets, "-j", str(jobs)]
    start = time.perf_counter()
    run(command, cwd=source, env=env, capture=True)
    return time.perf_counter() - start, summarize_new_ninja_edges(build, before, tuple(targets))


def reconfigure(source: Path, build: Path, configure: list[str], env: dict[str, str]) -> float:
    start = time.perf_counter()
    run(["cmake", "-S", str(source), "-B", str(build), *configure], cwd=source, env=env, capture=True)
    return time.perf_counter() - start


def uses_content_stable_compdb_stage(build: Path) -> bool:
    build_ninja = build / "build.ninja"
    if not build_ninja.exists():
        return False
    contents = build_ninja.read_text(encoding="utf-8", errors="replace")
    return "Staging compile commands for gentest target" in contents and "copy_if_different" in contents


def append_change(path: Path, tag: str) -> None:
    with path.open("a", encoding="utf-8") as stream:
        stream.write(f"// compile-campaign {tag}\n")


def rewrite_compdb(path: Path, *, add_unrelated: bool) -> None:
    # Preserve the semantic compilation database while intentionally replacing
    # the file (the codegen custom command depends on this file's timestamp).
    # The unrelated variant changes only an entry outside every measured target.
    parsed = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(parsed, list):
        raise RuntimeError(f"expected a JSON array compilation database: {path}")
    if add_unrelated:
        unrelated = path.parent / "__gentest_unrelated_compdb_entry__.cpp"
        parsed.append(
            {
                "directory": str(path.parent),
                "file": str(unrelated),
                "arguments": ["c++", "-c", str(unrelated)],
            }
        )
    path.write_text(json.dumps(parsed, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def restore_file(path: Path, contents: bytes, stat: os.stat_result) -> None:
    path.write_bytes(contents)
    os.chmod(path, stat.st_mode)
    os.utime(path, ns=(stat.st_atime_ns, stat.st_mtime_ns))


def validate_contract(
    name: str,
    profile: dict[str, object],
    active_tus: int,
    expects_codegen: bool,
    strict_downstream_noop: bool,
) -> None:
    edges = int(profile.get("unique_edges", 0))
    categories = profile.get("categories", {})
    assert isinstance(categories, dict)
    generated = int(categories.get("generated_tu_compile", 0))
    codegen = int(categories.get("codegen", 0))
    if name == "cold-build" and (edges < 1 or (expects_codegen and codegen < 1)):
        raise RuntimeError(f"cold-build invalidation contract failed: expected required build/codegen edges, got {profile}")
    if name == "no-op":
        if strict_downstream_noop and edges != 0:
            raise RuntimeError(f"no-op invalidation contract failed: expected zero Ninja edges, got {profile}")
        if not strict_downstream_noop and (codegen < 1 or generated or int(categories.get("compile", 0)) or int(categories.get("link_or_archive", 0))):
            raise RuntimeError(f"repository no-op contract failed: expected codegen-only persistent edge, got {profile}")
    if name == "source-edit" and (codegen < 1 or generated != 1):
        raise RuntimeError(f"source-edit invalidation contract failed: expected codegen and exactly one generated compile edge, got {profile}")
    if name == "private-header-edit" and (codegen < 1 or generated != 1):
        raise RuntimeError(f"private-header-edit invalidation contract failed: expected codegen and exactly one affected generated compile edge, got {profile}")
    if name == "shared-header-edit" and (codegen < 1 or generated != active_tus):
        raise RuntimeError(
            f"shared-header-edit invalidation contract failed: expected codegen and {active_tus} generated compile edges, got {profile}"
        )
    if name in {"equivalent-compdb-rewrite", "unrelated-compdb-rewrite"} and codegen < 1:
        raise RuntimeError(f"{name} invalidation contract failed: expected codegen edge, got {profile}")


def configured_codegen_caps(build: Path, expected: int) -> list[int]:
    ninja = build / "build.ninja"
    values: list[int] = []
    for line in ninja.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.lstrip().startswith("COMMAND =") and "gentest_codegen" in line:
            values.extend(codegen_job_values(line.split("=", 1)[1].strip()))
    if not values or any(value != expected for value in values):
        raise RuntimeError(f"cannot assert effective codegen cap {expected}; found {values or 'none'} in {ninja}")
    return values


def run_scenarios(
    source: Path,
    build: Path,
    configure: list[str],
    targets: list[str],
    scenarios: tuple[str, ...],
    jobs: int,
    warmups: int,
    samples: int,
    env: dict[str, str],
    synthetic: bool,
    active_tus: int,
    expects_codegen: bool,
    strict_downstream_noop: bool,
) -> dict[str, object]:
    result: dict[str, object] = {}
    for scenario in scenarios:
        measured: list[float] = []
        profiles: list[dict[str, object]] = []
        settling_profiles: list[dict[str, object]] = []
        total = warmups + samples
        for repetition in range(total):
            settling_profile: dict[str, object] | None = None
            if scenario == "cold-build":
                run(["cmake", "--build", str(build), "--target", "clean"], cwd=source, env=env, capture=True)
                elapsed, profile = build_target(source, build, targets, jobs, env)
            elif scenario == "no-op":
                elapsed, profile = build_target(source, build, targets, jobs, env)
            elif scenario == "reconfigure":
                elapsed = reconfigure(source, build, configure, env)
                _, profile = build_target(source, build, targets, jobs, env)
                categories = profile["categories"]
                assert isinstance(categories, dict)
                generated = int(categories.get("generated_tu_compile", 0))
                compiled = int(categories.get("compile", 0))
                linked = int(categories.get("link_or_archive", 0))
                codegen = int(categories.get("codegen", 0))
                if generated or compiled or linked:
                    raise RuntimeError(f"reconfigure invalidation contract failed: downstream compile/link work ran, got {profile}")
                if uses_content_stable_compdb_stage(build):
                    if codegen:
                        raise RuntimeError(f"reconfigure invalidation contract failed: stable compdb staging reran codegen, got {profile}")
                    profile["contract"] = "cmake-reconfigure-followed-by-compdb-staging-only-build"
                else:
                    if codegen < 1:
                        raise RuntimeError(f"reconfigure baseline contract failed: expected the unstaged compdb codegen edge, got {profile}")
                    profile["contract"] = "cmake-reconfigure-followed-by-codegen-only-build"
            else:
                changed_path: Path
                if scenario in {"equivalent-compdb-rewrite", "unrelated-compdb-rewrite"}:
                    changed_path = build / "compile_commands.json"
                elif not synthetic:
                    raise RuntimeError(f"{scenario} requires a generated fixture")
                elif scenario == "source-edit":
                    changed_path = source / "case_00.cpp"
                elif scenario == "private-header-edit":
                    changed_path = source / "private.hpp"
                elif scenario == "shared-header-edit":
                    changed_path = source / "shared.hpp"
                else:
                    raise RuntimeError(f"unknown scenario {scenario}")

                original = changed_path.read_bytes()
                original_stat = changed_path.stat()
                try:
                    if scenario in {"equivalent-compdb-rewrite", "unrelated-compdb-rewrite"}:
                        rewrite_compdb(changed_path, add_unrelated=scenario == "unrelated-compdb-rewrite")
                    else:
                        append_change(changed_path, scenario)
                    elapsed, profile = build_target(source, build, targets, jobs, env)
                    validate_contract(scenario, profile, active_tus, expects_codegen, strict_downstream_noop)
                finally:
                    restore_file(changed_path, original, original_stat)
                _, settling_profile = build_target(source, build, targets, jobs, env)
                if strict_downstream_noop and int(settling_profile["unique_edges"]) != 0:
                    raise RuntimeError(f"{scenario} did not settle to a zero-edge build after input restoration: {settling_profile}")
                if not strict_downstream_noop and (
                    int(settling_profile["categories"].get("codegen", 0)) < 1  # type: ignore[index]
                    or int(settling_profile["categories"].get("generated_tu_compile", 0))  # type: ignore[index]
                    or int(settling_profile["categories"].get("compile", 0))  # type: ignore[index]
                    or int(settling_profile["categories"].get("link_or_archive", 0))  # type: ignore[index]
                ):
                    raise RuntimeError(
                        f"{scenario} did not settle to the repository's persistent codegen-only edge: {settling_profile}"
                    )
            if scenario not in {"reconfigure", "source-edit", "private-header-edit", "shared-header-edit", "equivalent-compdb-rewrite", "unrelated-compdb-rewrite"}:
                validate_contract(scenario, profile, active_tus, expects_codegen, strict_downstream_noop)
            if repetition >= warmups:
                measured.append(elapsed)
                profiles.append(profile)
                if settling_profile is not None:
                    settling_profiles.append(settling_profile)
        result[scenario] = {
            "warmups": warmups,
            **median_mad(measured),
            "profiles": profiles,
            "settling_profiles": settling_profiles,
        }
    return result


def run_runtime_lane(source: Path, output: Path, configure: list[str], jobs: int, warmups: int, samples: int, env: dict[str, str]) -> dict[str, object]:
    build = output / "runtime-build"
    reconfigure(source, build, [*configure, "-Dgentest_BUILD_TESTING=OFF", "-DGENTEST_BUILD_CODEGEN=OFF"], env)
    return {
        "lane": "runtime",
        "build_dir": str(build),
        "scenarios": run_scenarios(
            source,
            build,
            [*configure, "-Dgentest_BUILD_TESTING=OFF", "-DGENTEST_BUILD_CODEGEN=OFF"],
            ["gentest_runtime"],
            RUNTIME_SCENARIOS,
            jobs,
            warmups,
            samples,
            env,
            False,
            0,
            False,
            True,
        ),
    }


def run_correctness_gate(lane: str, build: Path, targets: list[str], env: dict[str, str]) -> None:
    """Run built executables once, strictly outside the timed build samples."""
    if lane == "eight-tu-eight-binary":
        names = [f"campaign_binary_{index:02d}" for index in range(8)]
    else:
        names = targets
    for name in names:
        if lane == "repo-e2e":
            executable = build / "tests" / (f"{name}.exe" if os.name == "nt" else name)
        else:
            executable = build / (f"{name}.exe" if os.name == "nt" else name)
        if not executable.exists():
            raise RuntimeError(f"correctness gate executable was not produced: {executable}")
        run([str(executable)], cwd=build, env=env, capture=True)


def add_worktree(root: Path, destination: Path, revision: str) -> None:
    run(["git", "worktree", "add", "--detach", str(destination), revision], cwd=root, env=os.environ.copy(), capture=True)


def markdown(result: dict[str, object]) -> str:
    lines = ["# Gentest compile benchmark campaign", "", f"Baseline contract: `{BASELINE_COMMIT}`.", ""]
    provenance = result.get("provenance", {})
    if isinstance(provenance, dict):
        git_info = provenance.get("git", {})
        if isinstance(git_info, dict):
            lines.append(f"Measured HEAD: `{git_info.get('head')}`; dirty: `{git_info.get('dirty')}`.")
            lines.append("")
    lines.extend(["Raw samples are retained in `result.json`; median and MAD are descriptive, not pass/fail thresholds.", "", "| Lane | Cap | Scenario | Median (s) | MAD (s) |", "| --- | ---: | --- | ---: | ---: |"])
    lanes = result.get("lanes", [])
    if isinstance(lanes, list):
        for lane in lanes:
            if not isinstance(lane, dict):
                continue
            cap = lane.get("codegen_cap", "-")
            scenarios = lane.get("scenarios", {})
            if not isinstance(scenarios, dict):
                continue
            for name, stats in scenarios.items():
                if isinstance(stats, dict):
                    lines.append(f"| {lane.get('lane')} | {cap} | {name} | {float(stats['median_s']):.3f} | {float(stats['mad_s']):.3f} |")
    lines.append("")
    return "\n".join(lines)


def parse_csv(value: str, allowed: tuple[str, ...], flag: str) -> list[str]:
    entries = [entry.strip() for entry in value.split(",") if entry.strip()]
    invalid = [entry for entry in entries if entry not in allowed]
    if not entries or invalid:
        raise ValueError(f"{flag} must contain only {', '.join(allowed)} (invalid: {', '.join(invalid) or 'none'})")
    return entries


def parse_codegen_caps(value: str) -> list[tuple[str, int]]:
    labels = [part.strip() for part in value.split(",") if part.strip()]
    caps = [(label, parse_codegen_jobs(label)) for label in labels]
    if not caps:
        raise ValueError("--codegen-caps must not be empty")
    if len(set(labels)) != len(labels):
        raise ValueError("--codegen-caps must not repeat a cap label")
    if len({effective for _, effective in caps}) != len(caps):
        raise ValueError("--codegen-caps must not repeat an effective cap (for example, auto and 0)")
    return caps


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--cc", default=os.environ.get("CC", "clang-22"))
    parser.add_argument("--cxx", default=os.environ.get("CXX", "clang++-22"))
    parser.add_argument("--build-type", default="Release")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    parser.add_argument("--codegen-caps", default="1,2,4,8,auto")
    parser.add_argument("--samples", type=int, default=7)
    parser.add_argument("--warmups", type=int, default=2)
    parser.add_argument("--cache", choices=("off", "ccache", "sccache"), default="off")
    parser.add_argument("--lanes", default=",".join(DEFAULT_LANES))
    parser.add_argument("--repo-targets", default=",".join(REPO_TARGETS))
    parser.add_argument("--output-dir", default="build/compile-campaign")
    parser.add_argument("--allow-unavailable", action="store_true", help="Write an unavailable-compiler JSON result and return success")
    parser.add_argument("--allow-dirty", action="store_true", help="Use the dirty checkout for source input; provenance remains marked dirty")
    args = parser.parse_args()
    if args.jobs < 1 or args.samples < 1 or args.warmups < 0:
        parser.error("--jobs and --samples must be at least 1; --warmups must be non-negative")
    root = Path(git(Path.cwd(), "rev-parse", "--show-toplevel"))
    output = Path(args.output_dir)
    if not output.is_absolute():
        output = root / output
    try:
        require_fresh_output(output)
    except RuntimeError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    try:
        lanes = parse_csv(args.lanes, DEFAULT_LANES, "--lanes")
        caps = parse_codegen_caps(args.codegen_caps)
        caps_raw = [label for label, _ in caps]
    except ValueError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    compiler_available = shutil.which(args.cc) and shutil.which(args.cxx)
    provenance = machine_provenance(root, args.cc, args.cxx, "Ninja", args.build_type, args.cache)
    output.mkdir(parents=True, exist_ok=True)
    result_path = output / "result.json"
    if not compiler_available:
        result = {"status": "unavailable", "reason": f"requested compiler not found: cc={args.cc!r}, cxx={args.cxx!r}", "provenance": provenance}
        result_path.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        if args.allow_unavailable:
            print(result["reason"])
            return 0
        print(f"error: {result['reason']}", file=sys.stderr)
        return 2
    tracked_dirty = bool(provenance["git"]["tracked_dirty"])  # type: ignore[index]
    try:
        require_clean_checkout(tracked_dirty, args.allow_dirty)
    except RuntimeError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    temporary: tempfile.TemporaryDirectory[str] | None = None
    worktree: Path | None = None
    worktree_added = False
    env: dict[str, str] | None = None
    cache_metadata: dict[str, object] | None = None
    cache_finished = False
    try:
        env, cache_metadata = cache_environment(args.cache, output)
        source_input = root
        temporary = tempfile.TemporaryDirectory(prefix="gentest-compile-campaign-")
        worktree = Path(temporary.name) / "source"
        if not tracked_dirty:
            add_worktree(root, worktree, str(provenance["git"]["head"]))  # type: ignore[index]
            source_input = worktree
            worktree_added = True
        host_codegen = configure_host_codegen(source_input, output, args.cc, args.cxx, args.jobs, args.cache, env)
        configure = [
            *cmake_arguments(args.cc, args.cxx, args.build_type, args.cache),
            "-DGENTEST_BUILD_CODEGEN=OFF",
            f"-DGENTEST_CODEGEN_EXECUTABLE={host_codegen}",
        ]
        fixture_source = output / "fixture-source"
        fixture_targets = write_fixture(fixture_source, source_input)
        repository_build_root = fresh_repository_build_root(source_input) if "repo-e2e" in lanes else None
        entries: list[dict[str, object]] = []
        cap_labels = [raw for raw, _ in caps]
        cap_by_label = dict(caps)
        for round_index, cap_order in enumerate(alternating_order(cap_labels, args.samples)):
            # Each round includes one timed sample for every cap in alternating
            # direction.  Warmups are performed inside its isolated build.
            for cap_label in cap_order:
                cap = cap_by_label[cap_label]
                for lane in lanes:
                    if lane == "runtime":
                        # Runtime has no codegen process; include it once.
                        if cap_label != cap_labels[0]:
                            continue
                        entries.append(run_runtime_lane(source_input, output, configure, args.jobs, args.warmups if round_index == 0 else 0, 1, env))
                        continue
                    source = fixture_source if lane in fixture_targets else source_input
                    # Keep repository E2E build trees beneath their isolated
                    # source worktree.  CMake's depfile transformer then keeps
                    # absolute system-header dependencies valid; a sibling
                    # /tmp build can otherwise turn /usr/include into /tmp/usr.
                    build_root = output / "builds" if lane in fixture_targets else repository_build_root
                    if build_root is None:
                        raise RuntimeError("repository build root was not initialized")
                    build = build_root / lane / f"cap-{cap_label}" / f"round-{len(entries):03d}"
                    if lane in fixture_targets:
                        run(["cmake", "-S", str(source), "-B", str(build), *configure, f"-DGENTEST_CODEGEN_JOBS={cap}"], cwd=source, env=env, capture=True)
                        configured_codegen_caps(build, cap)
                        target = fixture_targets[lane]
                        scenarios = DEFAULT_SCENARIOS
                        synthetic = True
                        active_tus = 1 if lane == "one-tu" else 8
                    else:
                        repo_targets = [item.strip() for item in args.repo_targets.split(",") if item.strip()]
                        run(["cmake", "-S", str(source), "-B", str(build), *configure, "-Dgentest_BUILD_TESTING=ON", "-DGENTEST_ENABLE_PACKAGE_TESTS=OFF", f"-DGENTEST_CODEGEN_JOBS={cap}"], cwd=source, env=env, capture=True)
                        configured_codegen_caps(build, cap)
                        target = repo_targets
                        scenarios = REPO_SCENARIOS
                        synthetic = False
                        active_tus = 0
                    # Correctness gate happens only after the initial build,
                    # outside all timing samples.
                    build_target(source, build, target, args.jobs, env)
                    run_correctness_gate(lane, build, target, env)
                    entries.append({
                        "lane": lane,
                        "codegen_cap": cap_label,
                        "effective_codegen_cap": cap,
                        "effective_cli_values": configured_codegen_caps(build, cap),
                        "build_dir": str(build),
                        "targets": target,
                        "scenarios": run_scenarios(
                            source,
                            build,
                            configure,
                            target,
                            scenarios,
                            args.jobs,
                            args.warmups if round_index == 0 else 0,
                            1,
                            env,
                            synthetic,
                            active_tus,
                            True,
                            synthetic,
                        ),
                    })
        # Combine one-sample round entries into the advertised seven raw samples.
        combined: dict[tuple[str, str], dict[str, object]] = {}
        for entry in entries:
            lane = str(entry["lane"])
            cap = str(entry.get("codegen_cap", "-"))
            key = (lane, cap)
            destination = combined.setdefault(key, {k: v for k, v in entry.items() if k != "scenarios"})
            scenarios = entry.get("scenarios", {})
            assert isinstance(scenarios, dict)
            destination_scenarios = destination.setdefault("scenarios", {})
            assert isinstance(destination_scenarios, dict)
            for name, stats in scenarios.items():
                assert isinstance(stats, dict)
                target_stats = destination_scenarios.setdefault(name, {"profiles": [], "settling_profiles": [], "warmups": 0})
                assert isinstance(target_stats, dict)
                target_stats["profiles"].extend(stats["profiles"])
                target_stats["settling_profiles"].extend(stats.get("settling_profiles", []))
                target_stats.setdefault("samples_s", []).extend(stats["samples_s"])
                target_stats["warmups"] = int(target_stats["warmups"]) + int(stats["warmups"])
        final_lanes: list[dict[str, object]] = []
        for item in combined.values():
            scenarios = item["scenarios"]
            assert isinstance(scenarios, dict)
            for stats in scenarios.values():
                assert isinstance(stats, dict)
                samples = stats["samples_s"]
                assert isinstance(samples, list)
                profiles = stats["profiles"]
                warmup_count = stats.get("warmups", 0)
                settling_profiles = stats.get("settling_profiles", [])
                stats.clear()
                stats.update({"warmups": warmup_count, **median_mad(samples), "profiles": profiles, "settling_profiles": settling_profiles})
            final_lanes.append(item)
        finish_cache_metadata(args.cache, env, output, cache_metadata)
        cache_finished = True
        provenance["tools"]["host_codegen"] = version(host_codegen)  # type: ignore[index]
        result: dict[str, object] = {
            "status": "ok",
            "schema_version": 1,
            "provenance": provenance,
            "cache": cache_metadata,
            "configuration": {"samples": args.samples, "warmups": args.warmups, "jobs": args.jobs, "codegen_caps": caps_raw, "execution_order": alternating_order(cap_labels, args.samples)},
            "lanes": final_lanes,
        }
        result_path.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        (output / "summary.md").write_text(markdown(result), encoding="utf-8")
        log(f"[campaign] wrote {result_path}")
        if worktree_added and worktree is not None:
            run(["git", "worktree", "remove", "--force", str(worktree)], cwd=root, env=os.environ.copy(), capture=True)
        if temporary is not None:
            temporary.cleanup()
        return 0
    except (RuntimeError, subprocess.CalledProcessError, OSError, json.JSONDecodeError) as error:
        if worktree_added and worktree is not None:
            subprocess.run(["git", "worktree", "remove", "--force", str(worktree)], cwd=root, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if temporary is not None:
            temporary.cleanup()
        print(f"error: {error}", file=sys.stderr)
        return 1
    finally:
        if args.cache == "sccache" and env is not None and cache_metadata is not None and not cache_finished:
            shutdown_sccache(env, output, cache_metadata, check=False)


if __name__ == "__main__":
    raise SystemExit(main())
