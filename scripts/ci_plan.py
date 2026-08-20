#!/usr/bin/env python3
"""Compute conservative selective-CI decisions from a Git diff or path list."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from collections.abc import Iterable, Sequence
from pathlib import Path, PurePosixPath


LANES = (
    "cmake",
    "lint",
    "coverage",
    "cross_qemu",
    "bazel",
    "meson",
    "xmake",
    "measured",
)

BUILDSYSTEM_LANES = frozenset({"bazel", "meson", "xmake"})

FORCE_ALL_PATHS = frozenset(
    {
        "scripts/ci_plan.py",
        "tests/scripts/test_ci_plan.py",
    }
)

DEPENDENCY_PATHS = frozenset(
    {
        ".gitmodules",
        "MODULE.bazel.lock",
        "scripts/setup-vcpkg.sh",
        "vcpkg-configuration.json",
        "vcpkg.json",
    }
)

LINT_PATHS = frozenset(
    {
        ".clang-format",
        ".clang-tidy",
        ".cmake-format.py",
        ".editorconfig",
        "scripts/check_clang_format.sh",
        "scripts/check_clang_tidy.sh",
    }
)

BAZEL_PATHS = frozenset(
    {
        ".bazelrc",
        ".bazelversion",
        "BUILD",
        "BUILD.bazel",
        "MODULE.bazel",
        "WORKSPACE",
        "WORKSPACE.bazel",
        "scripts/bazel_codegen_check.sh",
    }
)

MESON_PATHS = frozenset({"meson.build", "meson_options.txt"})
XMAKE_PATHS = frozenset({"xmake.lua"})

CORE_PREFIXES = (
    "benchmarks/",
    "cmake/",
    "examples/",
    "include/",
    "port-templates/",
    "src/",
    "tests/",
    "third_party/",
    "tools/",
)

CORE_PATHS = frozenset(
    {
        "CMakeLists.txt",
        "CMakePresets.json",
        "CTestConfig.cmake",
    }
)

CORE_SUFFIXES = frozenset(
    {
        ".c",
        ".cc",
        ".cmake",
        ".cpp",
        ".cppm",
        ".cxx",
        ".h",
        ".hh",
        ".hpp",
        ".hxx",
        ".ixx",
        ".mpp",
    }
)

DOC_PREFIXES = ("docs/",)
DOC_NAMES = frozenset(
    {
        "AGENTS.md",
        "CHANGELOG.md",
        "CONTRIBUTING.md",
        "DEPRECATIONS.md",
        "LICENSE",
        "LICENSE.md",
        "README.md",
    }
)
DOC_SUFFIXES = frozenset({".adoc", ".md", ".rst"})

MEASURED_PREFIXES = (
    "cmake/",
    "include/gentest/",
    "src/",
    "tests/benchmarks/",
    "tools/src/",
)

MEASURED_PATHS = frozenset(
    {
        "CMakeLists.txt",
        "CMakePresets.json",
        "scripts/ci_measured_report_compare.sh",
        "scripts/compare_measured_reports.py",
        "tests/CMakeLists.txt",
        "tests/cmake/scripts/CheckMeasuredReportCompareScript.cmake",
        "tests/regressions/measured_report_coverage.cpp",
        "tools/CMakeLists.txt",
    }
)

DEPENDENCY_NAMES = frozenset(
    {
        "Cargo.lock",
        "Cargo.toml",
        "Pipfile",
        "Pipfile.lock",
        "conanfile.py",
        "conanfile.txt",
        "package-lock.json",
        "package.json",
        "poetry.lock",
        "pyproject.toml",
        "requirements.txt",
        "uv.lock",
    }
)


def all_enabled() -> dict[str, bool]:
    return {lane: True for lane in LANES}


def _lane_plan(enabled: Iterable[str] = ()) -> dict[str, bool]:
    selected = frozenset(enabled)
    return {lane: lane in selected for lane in LANES}


def _normalize_path(raw_path: str) -> str | None:
    if not isinstance(raw_path, str):
        return None
    path = raw_path
    while path.startswith("./"):
        path = path[2:]
    if (
        not path
        or path != path.strip()
        or path.startswith("/")
        or "\\" in path
        or not path.isprintable()
    ):
        return None
    parts = path.split("/")
    if any(part in {"", ".", ".."} for part in parts):
        return None
    return "/".join(parts)


def _is_dependency_path(path: str) -> bool:
    name = PurePosixPath(path).name
    return (
        path in DEPENDENCY_PATHS
        or name in DEPENDENCY_NAMES
        or name.startswith("requirements-")
        or name.endswith((".lock", ".lock.json"))
        or path.startswith(".github/dependabot")
    )


def _is_core_path(path: str) -> bool:
    return (
        path in CORE_PATHS
        or path.startswith(CORE_PREFIXES)
        or PurePosixPath(path).suffix.lower() in CORE_SUFFIXES
    )


def _is_doc_path(path: str) -> bool:
    return (
        path.startswith(DOC_PREFIXES)
        or path in DOC_NAMES
        or PurePosixPath(path).suffix.lower() in DOC_SUFFIXES
    )


def _is_measured_path(path: str) -> bool:
    return path in MEASURED_PATHS or path.startswith(MEASURED_PREFIXES)


def _specific_lanes(path: str) -> frozenset[str] | None:
    if path in LINT_PATHS:
        return frozenset({"lint"})
    if path in BAZEL_PATHS or path.startswith(("bazel/", "build_defs/")) or path.endswith(".bzl"):
        return frozenset({"bazel"})
    if path in MESON_PATHS or path.startswith("meson/"):
        return frozenset({"meson"})
    if path in XMAKE_PATHS or path.startswith("xmake/"):
        return frozenset({"xmake"})
    return None


def classify_paths(paths: Iterable[str]) -> dict[str, bool]:
    """Return lane decisions for changed repository-relative paths.

    Empty, malformed, and unrecognized path sets intentionally enable every lane.
    """

    normalized_paths: list[str] = []
    for raw_path in paths:
        path = _normalize_path(raw_path)
        if path is None:
            return all_enabled()
        normalized_paths.append(path)

    if not normalized_paths:
        return all_enabled()

    enabled: set[str] = set()
    for path in sorted(set(normalized_paths)):
        if (
            path in FORCE_ALL_PATHS
            or path.startswith(".github/workflows/")
            or _is_dependency_path(path)
        ):
            return all_enabled()

        if _is_doc_path(path):
            continue

        if _is_core_path(path):
            enabled.update(set(LANES) - {"measured"})
            if _is_measured_path(path):
                enabled.add("measured")
            continue

        lanes = _specific_lanes(path)
        if lanes is None and _is_measured_path(path):
            lanes = frozenset({"measured"})
        if lanes is None:
            return all_enabled()
        enabled.update(lanes)

    return _lane_plan(enabled)


def _git(repo: Path, *args: str) -> bytes | None:
    try:
        result = subprocess.run(
            ["git", *args],
            cwd=repo,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except (OSError, subprocess.CalledProcessError):
        return None
    return result.stdout


def _resolve_commit(repo: Path, revision: str) -> str | None:
    if not revision:
        return None
    output = _git(repo, "rev-parse", "--verify", "--end-of-options", f"{revision}^{{commit}}")
    if output is None:
        return None
    try:
        commit = output.decode("ascii").strip()
    except UnicodeDecodeError:
        return None
    if not commit or any(character not in "0123456789abcdefABCDEF" for character in commit):
        return None
    return commit


def changed_paths_from_git(repo: Path, base: str, head: str) -> list[str] | None:
    """Return paths changed from the base/head merge-base, or None on uncertainty."""

    base_commit = _resolve_commit(repo, base)
    head_commit = _resolve_commit(repo, head)
    if base_commit is None or head_commit is None:
        return None

    merge_base_output = _git(repo, "merge-base", "--all", base_commit, head_commit)
    if merge_base_output is None:
        return None
    try:
        merge_bases = merge_base_output.decode("ascii").splitlines()
    except UnicodeDecodeError:
        return None
    if len(merge_bases) != 1:
        return None
    merge_base = merge_bases[0]
    if not merge_base or any(character not in "0123456789abcdefABCDEF" for character in merge_base):
        return None

    output = _git(
        repo,
        "diff",
        "--name-only",
        "--no-renames",
        "--diff-filter=ACDMRTUXB",
        "-z",
        merge_base,
        head_commit,
        "--",
    )
    if output is None:
        return None

    raw_paths = output.split(b"\0")
    if raw_paths and raw_paths[-1] == b"":
        raw_paths.pop()
    try:
        return [path.decode("utf-8") for path in raw_paths]
    except UnicodeDecodeError:
        return None


def compute_plan(
    *,
    event_name: str = "",
    ref: str = "",
    paths: Iterable[str] | None = None,
    repo: Path = Path("."),
    base: str = "",
    head: str = "",
) -> dict[str, bool]:
    """Compute a plan, forcing all lanes for events and inputs that are uncertain."""

    event_name = event_name.strip()
    ref = ref.strip()

    if event_name == "workflow_dispatch":
        return all_enabled()
    if event_name == "push" and ref in {"master", "refs/heads/master"}:
        return all_enabled()
    if event_name not in {"", "pull_request", "push"}:
        return all_enabled()

    if paths is not None:
        return classify_paths(paths)

    if not base or not head:
        return all_enabled()
    changed_paths = changed_paths_from_git(repo, base, head)
    if changed_paths is None:
        return all_enabled()
    return classify_paths(changed_paths)


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--event", default=os.environ.get("GITHUB_EVENT_NAME", ""), help="GitHub event name")
    parser.add_argument("--ref", default=os.environ.get("GITHUB_REF", ""), help="GitHub ref")
    parser.add_argument("--base", default="", help="base commit or ref")
    parser.add_argument("--head", default="", help="head commit or ref")
    parser.add_argument(
        "--paths",
        nargs="*",
        default=None,
        metavar="PATH",
        help="classify explicit repository-relative paths instead of reading Git",
    )
    parser.add_argument("--repo", type=Path, default=Path("."), help="Git repository used with --base/--head")
    parser.add_argument(
        "--github-output",
        type=Path,
        default=Path(os.environ["GITHUB_OUTPUT"]) if os.environ.get("GITHUB_OUTPUT") else None,
        help="append run_<lane>=true|false values to this GitHub output file",
    )
    return parser


def _output_lines(plan: dict[str, bool]) -> list[str]:
    return [f"run_{lane}={'true' if plan[lane] else 'false'}" for lane in LANES]


def main(argv: Sequence[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    plan = compute_plan(
        event_name=args.event,
        ref=args.ref,
        paths=args.paths,
        repo=args.repo,
        base=args.base,
        head=args.head,
    )
    lines = _output_lines(plan)
    print("\n".join(lines))
    if args.github_output is not None:
        with args.github_output.open("a", encoding="utf-8", newline="\n") as output:
            output.write("\n".join(lines))
            output.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
