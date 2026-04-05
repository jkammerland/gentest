#!/usr/bin/env python3
"""Generate scoped repo coverage reports and enforce aggregate thresholds."""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
import tomllib
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Sequence

from coverage_hygiene import DEFAULT_POLICY_PATH, _read_policy_config


DEFAULT_REPORT_POLICY = {
    "fail_under_line": 75.0,
    "fail_under_branch": 45.0,
    "top_files": 10,
    "gcov_ignore_parse_errors": "negative_hits.warn_once_per_file",
}


def _read_report_policy(config_path: Path) -> Dict[str, Any]:
    policy = dict(DEFAULT_REPORT_POLICY)
    if not config_path.exists():
        return policy

    data = tomllib.loads(config_path.read_text(encoding="utf-8"))
    report = data.get("report", {})
    if report is None:
        return policy
    if not isinstance(report, dict):
        raise RuntimeError(f"coverage report config in '{config_path}' must be a TOML table")

    for key in ("fail_under_line", "fail_under_branch"):
        value = report.get(key, policy[key])
        if not isinstance(value, (int, float)):
            raise RuntimeError(f"coverage report key '{key}' in '{config_path}' must be numeric")
        policy[key] = float(value)

    value = report.get("top_files", policy["top_files"])
    if not isinstance(value, int) or value < 1:
        raise RuntimeError(f"coverage report key 'top_files' in '{config_path}' must be a positive integer")
    policy["top_files"] = value

    value = report.get("gcov_ignore_parse_errors", policy["gcov_ignore_parse_errors"])
    if not isinstance(value, str):
        raise RuntimeError(f"coverage report key 'gcov_ignore_parse_errors' in '{config_path}' must be a string")
    policy["gcov_ignore_parse_errors"] = value
    return policy


def _parse_cmake_cache(cache_path: Path) -> Dict[str, str]:
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


def _compiler_family(build_dir: Path) -> str:
    cache = _parse_cmake_cache(build_dir / "CMakeCache.txt")
    compiler_id = cache.get("CMAKE_CXX_COMPILER_ID", "") or cache.get("CMAKE_C_COMPILER_ID", "")
    compiler = cache.get("CMAKE_CXX_COMPILER", "") or cache.get("CMAKE_C_COMPILER", "")
    compiler_text = f"{compiler_id} {compiler}".lower()
    if "clang" in compiler_text:
        return "clang"
    if "gnu" in compiler_text or re.search(r"(^|[\\/])(g\+\+|gcc|cc|c\+\+)([.-]|$)", compiler_text):
        return "gcc"
    return ""


def _resolve_gcov_command(build_dir: Path, explicit: Sequence[str] | None) -> List[str]:
    if explicit:
        command = list(explicit)
    else:
        family = _compiler_family(build_dir)
        if family == "clang":
            command = ["llvm-cov", "gcov"]
        else:
            command = ["gcov"]

    binary = shutil.which(command[0])
    if not binary:
        raise RuntimeError(
            f"Coverage frontend '{command[0]}' was not found in PATH. "
            "Install the matching gcov frontend or pass --gcov explicitly."
        )
    command[0] = binary
    return command


def _path_regex(path: Path, directory: bool) -> str:
    suffix = r"(/|$)" if directory else r"$"
    return re.escape(path.as_posix()) + suffix


def _format_percent(value: Optional[float]) -> str:
    if value is None:
        return "n/a"
    return f"{value:.1f}%"


def _format_fraction(covered: Optional[int], total: Optional[int]) -> str:
    if covered is None or total is None:
        return "n/a"
    return f"{covered}/{total}"


def _status_label(value: Optional[float], threshold: Optional[float]) -> str:
    if threshold is None:
        return "info"
    if value is None:
        return "fail"
    return "pass" if value >= threshold else "fail"


def _status_emoji(status: str) -> str:
    if status == "pass":
        return "✅"
    if status == "info":
        return "ℹ️"
    return "❌"


def _summary_table_row(
    metric: str,
    covered: Optional[int],
    total: Optional[int],
    pct: Optional[float],
    threshold: Optional[float],
) -> str:
    status = _status_label(pct, threshold)
    threshold_label = "report only" if threshold is None else f"{threshold:.1f}%"
    return (
        f"| {metric} | {_format_fraction(covered, total)} | {_format_percent(pct)} | "
        f"{threshold_label} | {_status_emoji(status)} |"
    )


def _write_summary(
    summary_path: Path,
    build_dir: Path,
    data: Dict[str, Any],
    line_threshold: float,
    branch_threshold: float,
    top_files: int,
) -> None:
    files = list(data.get("files", []))

    def _sort_key(item: Dict[str, Any]) -> tuple[float, float, str]:
        branch = item.get("branch_percent")
        line = item.get("line_percent")
        return (
            101.0 if branch is None else float(branch),
            101.0 if line is None else float(line),
            str(item.get("filename", "")),
        )

    weakest = sorted(files, key=_sort_key)[:top_files]

    lines = [
        "# Coverage Report",
        "",
        f"Build dir: `{build_dir}`",
        "",
        "Scope: repo-owned files in the implementation trees under `src/` and `tools/src/`, including internal "
        "headers in those trees. Intentional exemptions from `scripts/coverage_hygiene.toml` are excluded from totals.",
        "",
        "## Overall coverage",
        "",
        "| Metric | Covered / Total | Coverage | Threshold | Status |",
        "|--------|-----------------|----------|-----------|--------|",
        _summary_table_row(
            "Lines",
            data.get("line_covered"),
            data.get("line_total"),
            data.get("line_percent"),
            line_threshold,
        ),
        _summary_table_row(
            "Branches",
            data.get("branch_covered"),
            data.get("branch_total"),
            data.get("branch_percent"),
            branch_threshold,
        ),
        _summary_table_row(
            "Functions",
            data.get("function_covered"),
            data.get("function_total"),
            data.get("function_percent"),
            None,
        ),
        "",
        f"## Lowest branch coverage files (top {len(weakest)})",
        "",
        "| File | Lines | Branches | Functions |",
        "|------|-------|----------|-----------|",
    ]

    for item in weakest:
        filename = str(item.get("filename", ""))
        lines.append(
            f"| `{filename}` | {_format_percent(item.get('line_percent'))} | "
            f"{_format_percent(item.get('branch_percent'))} | {_format_percent(item.get('function_percent'))} |"
        )

    summary_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def _metric_failures(data: Dict[str, Any], line_threshold: float, branch_threshold: float) -> List[str]:
    failures: List[str] = []
    line_pct = data.get("line_percent")
    branch_pct = data.get("branch_percent")
    if line_pct is None or float(line_pct) < line_threshold:
        failures.append(f"line coverage {_format_percent(line_pct)} < required {line_threshold:.1f}%")
    if branch_pct is None or float(branch_pct) < branch_threshold:
        failures.append(f"branch coverage {_format_percent(branch_pct)} < required {branch_threshold:.1f}%")
    return failures


def _gcovr_command(
    project_root: Path,
    build_dir: Path,
    output_dir: Path,
    roots: Iterable[str],
    exclude_prefix: Iterable[str],
    intentional: Iterable[str],
    gcov_cmd: Sequence[str],
    gcov_ignore_parse_errors: str,
) -> List[str]:
    cmd = [
        sys.executable,
        "-m",
        "gcovr",
        "-r",
        str(project_root),
        str(build_dir),
        "--gcov-executable",
        " ".join(gcov_cmd),
        "--gcov-ignore-parse-errors",
        gcov_ignore_parse_errors,
        "--txt-summary",
        "--json-summary",
        str(output_dir / "summary.json"),
        "--json-summary-pretty",
        "--html-details",
        str(output_dir / "index.html"),
        "--sort",
        "uncovered-percent",
        "--sort-branches",
    ]

    for root in roots:
        root_path = (project_root / root).resolve(strict=False)
        cmd.extend(["--filter", _path_regex(root_path, directory=True)])

    for item in exclude_prefix:
        exclude_path = (project_root / item).resolve(strict=False)
        cmd.extend(["--exclude", _path_regex(exclude_path, directory=True)])

    for item in intentional:
        exclude_path = (project_root / item).resolve(strict=False)
        cmd.extend(["--exclude", _path_regex(exclude_path, directory=False)])

    return cmd


def main() -> int:
    config_parser = argparse.ArgumentParser(add_help=False)
    config_parser.add_argument(
        "--config",
        default=str(DEFAULT_POLICY_PATH),
        help="Path to the shared coverage policy TOML file.",
    )
    config_args, remaining = config_parser.parse_known_args()
    config_path = Path(config_args.config)
    hygiene_policy = _read_policy_config(config_path)
    report_policy = _read_report_policy(config_path)

    parser = argparse.ArgumentParser(parents=[config_parser])
    parser.add_argument(
        "--build-dir",
        default="build/coverage-system",
        help="Coverage-instrumented build directory.",
    )
    parser.add_argument(
        "--output-dir",
        default="",
        help="Output directory for summary/json/html reports. Defaults to <build-dir>/coverage-report.",
    )
    parser.add_argument(
        "--gcov",
        nargs="+",
        default=None,
        help="Explicit gcov-compatible frontend command. Defaults to a build-aware auto selection.",
    )
    parser.add_argument(
        "--fail-under-line",
        type=float,
        default=report_policy["fail_under_line"],
        help="Fail when aggregate line coverage falls below this percentage.",
    )
    parser.add_argument(
        "--fail-under-branch",
        type=float,
        default=report_policy["fail_under_branch"],
        help="Fail when aggregate branch coverage falls below this percentage.",
    )
    parser.add_argument(
        "--top-files",
        type=int,
        default=report_policy["top_files"],
        help="Number of lowest-branch-coverage files to include in the Markdown summary.",
    )
    args = parser.parse_args(remaining)

    project_root = Path(__file__).resolve().parents[1]
    build_dir = Path(args.build_dir)
    if not build_dir.is_absolute():
        build_dir = project_root / build_dir
    build_dir = build_dir.resolve(strict=False)
    if not build_dir.exists():
        raise FileNotFoundError(f"Coverage build directory not found: {build_dir}")

    output_dir = build_dir / "coverage-report" if args.output_dir == "" else Path(args.output_dir)
    if not output_dir.is_absolute():
        output_dir = project_root / output_dir
    output_dir = output_dir.resolve(strict=False)
    output_dir.mkdir(parents=True, exist_ok=True)

    gcov_cmd = _resolve_gcov_command(build_dir, args.gcov)
    gcovr_cmd = _gcovr_command(
        project_root,
        build_dir,
        output_dir,
        hygiene_policy["roots"],
        hygiene_policy["exclude_prefix"],
        hygiene_policy["intentional"],
        gcov_cmd,
        report_policy["gcov_ignore_parse_errors"],
    )

    proc = subprocess.run(
        gcovr_cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )
    if proc.stdout:
        print(proc.stdout, end="")
    if proc.returncode != 0:
        return proc.returncode

    summary_json = output_dir / "summary.json"
    if not summary_json.exists():
        raise FileNotFoundError(f"gcovr did not produce the expected summary file: {summary_json}")
    data = json.loads(summary_json.read_text(encoding="utf-8"))

    summary_md = output_dir / "summary.md"
    _write_summary(
        summary_md,
        build_dir,
        data,
        args.fail_under_line,
        args.fail_under_branch,
        args.top_files,
    )

    failures = _metric_failures(data, args.fail_under_line, args.fail_under_branch)
    print(f"Coverage summary written to {summary_md}")
    print(f"Coverage details written to {output_dir / 'index.html'}")
    if failures:
        print("Coverage threshold failures:")
        for failure in failures:
            print(f"  - {failure}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
