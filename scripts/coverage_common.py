#!/usr/bin/env python3
"""Shared coverage script policy and tool resolution helpers."""

from __future__ import annotations

import re
import shutil
import tomllib
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence


DEFAULT_BUILD_DIR = "build/coverage-system"
DEFAULT_POLICY = {
    "roots": ["src", "tools/src"],
    "exclude_prefix": [],
    "intentional": [
        "src/gentest_anchor.cpp",
        "tools/src/tooling_support.cpp",
        "src/runtime_context.cpp",
    ],
    "no_exec": [],
    "intentional_by_compiler": {},
    "fail_on": "missing_obj,missing_gcda,stamp_mismatch,no_match,gcov_error",
    "warn_on": "zero_hits",
}
DEFAULT_POLICY_PATH = Path(__file__).with_name("coverage_hygiene.toml")


def read_policy_config(config_path: Path) -> Dict[str, Any]:
    policy = dict(DEFAULT_POLICY)
    if not config_path.exists():
        return policy

    data = tomllib.loads(config_path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise RuntimeError(f"coverage policy config at '{config_path}' must be a TOML table")

    for key in ("roots", "exclude_prefix", "intentional", "no_exec"):
        value = data.get(key, policy[key])
        if not isinstance(value, list) or any(not isinstance(item, str) for item in value):
            raise RuntimeError(f"coverage policy key '{key}' in '{config_path}' must be a list of strings")
        policy[key] = list(value)

    for key in ("fail_on", "warn_on"):
        value = data.get(key, policy[key])
        if not isinstance(value, str):
            raise RuntimeError(f"coverage policy key '{key}' in '{config_path}' must be a string")
        policy[key] = value

    conditional = data.get("intentional_by_compiler", {})
    if not isinstance(conditional, dict):
        raise RuntimeError("coverage policy key 'intentional_by_compiler' must be a table")
    for family, paths in conditional.items():
        if family not in {"clang", "gcc"} or not isinstance(paths, list) or any(not isinstance(path, str) for path in paths):
            raise RuntimeError("compiler-specific coverage exemptions require clang/gcc lists of source paths")
    policy["intentional_by_compiler"] = conditional

    return policy


def normalize_build_dir(project_root: Path, build_dir: str | Path) -> Path:
    path = Path(build_dir)
    if not path.is_absolute():
        path = project_root / path
    return path.resolve(strict=False)


def parse_cmake_cache(cache_path: Path) -> Dict[str, str]:
    entries: Dict[str, str] = {}
    if not cache_path.exists():
        return entries
    for raw_line in cache_path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("//") or line.startswith("#"):
            continue
        if "=" not in line or ":" not in line:
            continue
        lhs, rhs = line.split("=", 1)
        key, _, _ = lhs.partition(":")
        entries[key] = rhs
    return entries


def active_cxx_compiler_id(build_dir: Path, cache: Dict[str, str]) -> Optional[str]:
    """None means absent metadata; an empty ID means unusable/ambiguous metadata."""
    files = list(build_dir.glob("CMakeFiles/*/CMakeCXXCompiler.cmake"))
    version = [cache.get(f"CMAKE_CACHE_{part}_VERSION", "") for part in ("MAJOR", "MINOR", "PATCH")]
    if any(version):
        if not all(re.fullmatch(r"\d+", part) for part in version):
            return ""
        compiler_file = build_dir / "CMakeFiles" / ".".join(version) / "CMakeCXXCompiler.cmake"
        if not compiler_file.is_file():
            # Never select a stale version directory in place of the active one.
            return "" if files else None
    else:
        # Without the cache's active version, even one file could be stale.
        return "" if files else None

    matches = re.findall(r'^\s*set\(CMAKE_CXX_COMPILER_ID\s+"([^"]*)"\s*\)\s*$',
                         compiler_file.read_text(encoding="utf-8"), re.MULTILINE)
    return matches[0] if len(matches) == 1 else ""


def compiler_family(build_dir: Path) -> str:
    cache = parse_cmake_cache(build_dir / "CMakeCache.txt")
    compiler_id = active_cxx_compiler_id(build_dir, cache)
    if compiler_id is None:
        compiler_id = cache.get("CMAKE_CXX_COMPILER_ID")
    if compiler_id is not None:
        # A known foreign compiler must not inherit an exemption from its path.
        return {"Clang": "clang", "AppleClang": "clang", "GNU": "gcc"}.get(compiler_id, "")

    # Only unambiguous executable names are usable without CMake's identity.
    compiler = re.split(r"[\\/]", cache.get("CMAKE_CXX_COMPILER", ""))[-1].lower()
    if re.fullmatch(r"clang(?:\+\+|-cl)?(?:-\d+(?:\.\d+)*)?(?:\.exe)?", compiler):
        return "clang"
    if re.fullmatch(r"(?:g\+\+|gcc)(?:-\d+(?:\.\d+)*)?(?:\.exe)?", compiler):
        return "gcc"
    return ""


def intentional_sources(policy: Dict[str, Any], build_dir: Path) -> List[str]:
    """Apply only exemptions justified for the compiler that built the objects."""
    conditional = policy.get("intentional_by_compiler", {})
    return [*policy["intentional"], *conditional.get(compiler_family(build_dir), [])]


def resolve_gcov_command(build_dir: Path, explicit: Optional[Sequence[str]]) -> List[str]:
    if explicit:
        command = list(explicit)
    else:
        family = compiler_family(build_dir)
        command = ["llvm-cov", "gcov"] if family == "clang" else ["gcov"]

    binary = shutil.which(command[0])
    if not binary:
        raise RuntimeError(
            f"Coverage frontend '{command[0]}' was not found in PATH. "
            "Install the matching gcov frontend or pass --gcov explicitly."
        )
    command[0] = binary
    return command
