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


def compiler_family(build_dir: Path) -> str:
    cache = parse_cmake_cache(build_dir / "CMakeCache.txt")
    compiler_id = cache.get("CMAKE_CXX_COMPILER_ID", "") or cache.get("CMAKE_C_COMPILER_ID", "")
    compiler = cache.get("CMAKE_CXX_COMPILER", "") or cache.get("CMAKE_C_COMPILER", "")
    compiler_text = f"{compiler_id} {compiler}".lower()
    if "clang" in compiler_text:
        return "clang"
    if "gnu" in compiler_text or re.search(r"(^|[\\/])(g\+\+|gcc|cc|c\+\+)([.-]|$)", compiler_text):
        return "gcc"
    return ""


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
