#!/usr/bin/env python3
"""Compare Gentest measured JSON/CSV reports against a saved baseline."""

from __future__ import annotations

import argparse
import csv
import html
import io
import json
import math
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Sequence


DEFAULT_METRICS = ("median_ns_per_item", "p95_ns_per_item", "stddev_ns_per_item")
NAMED_METRICS = ("median_ns", "p95_ns", "stddev_ns")
SUMMARY_TABLE_SUFFIX = ".summary"
MEASUREMENTS_TABLE_SUFFIX = ".measurements"


@dataclass(frozen=True)
class CaseKey:
    kind: str
    name: str
    series: str | None = None


@dataclass
class CaseMetrics:
    suite: str = ""
    metrics: dict[str, float | None] | None = None
    execution_mode: str = "calibrated"
    time_source: str = "runner"
    sampling_kind: str = "epoch-average"

    def __post_init__(self) -> None:
        if self.metrics is None:
            self.metrics = {}


@dataclass
class MetricComparison:
    key: CaseKey
    metric: str
    baseline: float
    current: float
    delta: float
    delta_pct: float | None
    status: str


@dataclass
class ComparisonResult:
    comparisons: list[MetricComparison]
    regressions: list[MetricComparison]
    improvements: list[MetricComparison]
    missing_cases: list[CaseKey]
    new_cases: list[CaseKey]
    omitted_metrics: list[str]
    unavailable_metrics: list[str]


def _as_float(value: Any) -> float | None:
    if isinstance(value, bool) or value is None:
        return None
    if isinstance(value, (int, float)):
        number = float(value)
    elif isinstance(value, str):
        try:
            number = float(value)
        except ValueError:
            return None
    else:
        return None
    if not math.isfinite(number):
        return None
    return number


def _case_kind(report: str, table_id: str) -> str:
    if report:
        return report
    if "." in table_id:
        return table_id.split(".", 1)[0]
    return "measured"


def _add_case(cases: dict[CaseKey, CaseMetrics], key: CaseKey, metrics: CaseMetrics, source: Path) -> None:
    if key in cases:
        raise ValueError(f"{source}: duplicate measured row for {_case_label(key)}")
    cases[key] = metrics


def _case_label(key: CaseKey) -> str:
    label = f"{key.kind}/{key.name}"
    return label if key.series is None else f"{label} [series={key.series!r}]"


def _sorted_keys(keys: Iterable[CaseKey]) -> list[CaseKey]:
    return sorted(keys, key=lambda key: (key.kind, key.name, key.series is not None, key.series or ""))


def _is_measured_table(table_id: str) -> bool:
    return table_id.endswith((SUMMARY_TABLE_SUFFIX, MEASUREMENTS_TABLE_SUFFIX))


def _row_identity(kind: str, table_id: str, row: dict[str, Any], path: Path) -> CaseKey:
    series = None
    if table_id.endswith(MEASUREMENTS_TABLE_SUFFIX):
        series = row.get("series")
        if not isinstance(series, str):
            raise ValueError(f"{path}: named measurement row must contain a string 'series'")
    return CaseKey(kind, str(row["benchmark"]), series)


def _row_metrics(key: CaseKey, row: dict[str, Any], path: Path) -> CaseMetrics:
    # Old primary reports predate these fields but used calibrated runner timing.
    legacy_batch = row.get("mode") == "batch" and (_as_float(row.get("calls_per_sample")) or 1) > 1
    legacy_jitter_sampling = "batch-average" if legacy_batch else "per-call"
    sampling = "epoch-average" if key.kind == "bench" else row.get("sample_kind", legacy_jitter_sampling)
    if key.series is not None:
        sampling = "observation"
    semantics = {
        "execution_mode": row.get("execution_mode", "calibrated"),
        "time_source": row.get("time_source", "runner"),
        "sampling_kind": row.get("sampling_kind", sampling),
    }
    allowed = {
        "execution_mode": {"calibrated", "once"},
        "time_source": {"runner", "supplied"},
        "sampling_kind": {"epoch-average", "per-call", "batch-average", "single-call", "observation"},
    }
    for field, value in semantics.items():
        if not isinstance(value, str) or value not in allowed[field]:
            raise ValueError(f"{path}: {_case_label(key)} has invalid {field}: {value!r}")
    case = CaseMetrics(suite=str(row.get("suite", "")), **semantics)
    for field, value in row.items():
        number = _as_float(value)
        if number is not None or value is None:
            case.metrics[field] = number
    return case


def _load_json_report(path: Path, payload: dict[str, Any]) -> dict[CaseKey, CaseMetrics]:
    cases: dict[CaseKey, CaseMetrics] = {}
    tables = payload.get("tables")
    if not isinstance(tables, list):
        raise ValueError(f"{path}: JSON report must contain a 'tables' array")

    for table in tables:
        if not isinstance(table, dict):
            continue
        table_id = str(table.get("id", ""))
        if not _is_measured_table(table_id):
            continue
        rows = table.get("rows")
        if not isinstance(rows, list):
            continue
        kind = _case_kind(str(table.get("report", payload.get("report", ""))), table_id)
        for row in rows:
            if not isinstance(row, dict) or "benchmark" not in row:
                continue
            key = _row_identity(kind, table_id, row, path)
            _add_case(cases, key, _row_metrics(key, row, path), path)
    return cases


def _load_csv_report(path: Path, text: str) -> dict[CaseKey, CaseMetrics]:
    grouped: dict[tuple[str, str, str], dict[str, tuple[str, str]]] = {}
    reader = csv.DictReader(io.StringIO(text))
    expected = ["report", "table", "row", "field", "type", "value"]
    if reader.fieldnames != expected:
        raise ValueError(f"{path}: CSV report must use header {','.join(expected)}")

    for record in reader:
        if None in record or any(record.get(field) is None for field in expected):
            raise ValueError(f"{path}: malformed CSV record at line {reader.line_num}")
        report = record["report"]
        table = record["table"]
        if not _is_measured_table(table):
            continue
        row = record["row"]
        fields = grouped.setdefault((report, table, row), {})
        if record["field"] in fields:
            raise ValueError(f"{path}: duplicate CSV field at line {reader.line_num}")
        fields[record["field"]] = (record["type"], record["value"])

    cases: dict[CaseKey, CaseMetrics] = {}
    for (report, table, _row), fields in grouped.items():
        benchmark = fields.get("benchmark")
        if benchmark is None:
            continue
        values: dict[str, Any] = {}
        for field, (field_type, value) in fields.items():
            values[field] = _as_float(value) if field_type == "number" else None if field_type == "null" else value
        key = _row_identity(_case_kind(report, table), table, values, path)
        _add_case(cases, key, _row_metrics(key, values, path), path)
    return cases


def load_report(path: Path) -> dict[CaseKey, CaseMetrics]:
    text = path.read_text(encoding="utf-8")
    stripped = text.lstrip()
    if not stripped:
        raise ValueError(f"{path}: report is empty")
    if stripped.startswith("{"):
        payload = json.loads(text)
        if not isinstance(payload, dict):
            raise ValueError(f"{path}: JSON report root must be an object")
        return _load_json_report(path, payload)
    return _load_csv_report(path, text)


def _require_cases(label: str, path: Path, cases: dict[CaseKey, CaseMetrics]) -> None:
    if not cases:
        raise ValueError(f"{path}: {label} report contains no measured summary rows")


def _unique_metrics(metrics: Sequence[str]) -> list[str]:
    unique: list[str] = []
    for metric in metrics:
        if metric not in unique:
            unique.append(metric)
    return unique


def compare_reports(
    baseline: dict[CaseKey, CaseMetrics],
    current: dict[CaseKey, CaseMetrics],
    metrics: Sequence[str] | None,
    fail_regression_pct: float,
    min_regression_delta: float,
) -> ComparisonResult:
    baseline_keys = set(baseline)
    current_keys = set(current)
    common_keys = _sorted_keys(baseline_keys & current_keys)

    comparisons: list[MetricComparison] = []
    regressions: list[MetricComparison] = []
    improvements: list[MetricComparison] = []
    omitted_metrics: list[str] = []
    unavailable_metrics: list[str] = []

    for key in common_keys:
        for field in ("execution_mode", "time_source", "sampling_kind"):
            before_semantics = getattr(baseline[key], field)
            after_semantics = getattr(current[key], field)
            if before_semantics != after_semantics:
                raise ValueError(
                    f"{_case_label(key)}: incompatible {field}: baseline={before_semantics!r}, current={after_semantics!r}"
                )

        baseline_metrics = baseline[key].metrics or {}
        current_metrics = current[key].metrics or {}
        defaults = NAMED_METRICS if key.series is not None else DEFAULT_METRICS
        metric_names = _unique_metrics(metrics if metrics is not None else defaults)
        for metric in metric_names:
            in_baseline = metric in baseline_metrics
            in_current = metric in current_metrics
            if in_baseline != in_current:
                missing_from = "current" if in_baseline else "baseline"
                raise ValueError(f"{_case_label(key)}: requested metric '{metric}' is missing from the {missing_from} report")
            if not in_baseline:
                omitted = f"{_case_label(key)}:{metric}"
                omitted_metrics.append(omitted)
                continue
            before = baseline_metrics[metric]
            after = current_metrics[metric]
            if before is None or after is None:
                sides = "baseline" if before is None else "current"
                if before is None and after is None:
                    sides = "baseline and current"
                unavailable_metrics.append(f"{_case_label(key)}:{metric}: unavailable in {sides}")
                continue
            delta = after - before
            if before == 0.0:
                delta_pct = 0.0 if delta == 0.0 else math.copysign(math.inf, delta)
            else:
                delta_pct = delta / before * 100.0
            status = "stable"
            if delta < 0.0:
                status = "improvement"
            if delta > min_regression_delta and delta_pct is not None and delta_pct > fail_regression_pct:
                status = "regression"

            comparison = MetricComparison(
                key=key,
                metric=metric,
                baseline=before,
                current=after,
                delta=delta,
                delta_pct=delta_pct,
                status=status,
            )
            comparisons.append(comparison)
            if status == "regression":
                regressions.append(comparison)
            elif status == "improvement":
                improvements.append(comparison)

    return ComparisonResult(
        comparisons=comparisons,
        regressions=regressions,
        improvements=improvements,
        missing_cases=_sorted_keys(baseline_keys - current_keys),
        new_cases=_sorted_keys(current_keys - baseline_keys),
        omitted_metrics=omitted_metrics,
        unavailable_metrics=unavailable_metrics,
    )


def _format_number(value: float) -> str:
    if value == 0.0:
        return "0"
    if abs(value) >= 1000.0 or abs(value) < 0.001:
        return f"{value:.6g}"
    return f"{value:.3f}".rstrip("0").rstrip(".")


def _format_delta_pct(value: float | None) -> str:
    if value is None:
        return "n/a"
    return f"{value:+.2f}%"


def _markdown_text(value: object) -> str:
    escaped = html.escape(str(value), quote=False)
    return escaped.replace("|", "&#124;").replace("\r\n", "\n").replace("\r", "\n").replace("\n", "<br>")


def _markdown_code(value: object) -> str:
    return f"<code>{_markdown_text(value)}</code>"


def _append_comparison_table(lines: list[str], title: str, rows: Iterable[MetricComparison]) -> None:
    rows = list(rows)
    lines.extend([f"## {title}", ""])
    if not rows:
        lines.extend(["None.", ""])
        return
    lines.extend(
        [
            "| Status | Kind | Benchmark | Series | Metric | Baseline | Current | Delta | Delta % |",
            "|--------|------|-----------|--------|--------|----------|---------|-------|---------|",
        ]
    )
    for row in rows:
        lines.append(
            f"| {_markdown_text(row.status)} | {_markdown_text(row.key.kind)} | "
            f"{_markdown_code(row.key.name)} | {_markdown_series(row.key)} | {_markdown_code(row.metric)} | "
            f"{_format_number(row.baseline)} | {_format_number(row.current)} | "
            f"{_format_number(row.delta)} | {_format_delta_pct(row.delta_pct)} |"
        )
    lines.append("")


def _append_case_table(lines: list[str], title: str, rows: Iterable[CaseKey]) -> None:
    rows = list(rows)
    lines.extend([f"## {title}", ""])
    if not rows:
        lines.extend(["None.", ""])
        return
    lines.extend(["| Kind | Benchmark | Series |", "|------|-----------|--------|"])
    for row in rows:
        lines.append(f"| {_markdown_text(row.kind)} | {_markdown_code(row.name)} | {_markdown_series(row)} |")
    lines.append("")


def _markdown_series(key: CaseKey) -> str:
    return "(primary)" if key.series is None else _markdown_code(key.series)


def render_markdown(
    baseline_path: Path,
    current_path: Path,
    metrics: Sequence[str] | None,
    result: ComparisonResult,
    fail_regression_pct: float,
    min_regression_delta: float,
    fail_on_new: bool,
    fail_on_missing: bool,
) -> str:
    stable_count = sum(1 for comparison in result.comparisons if comparison.status == "stable")
    if metrics is None:
        metric_description = (
            "primary " + ", ".join(_markdown_code(metric) for metric in DEFAULT_METRICS)
            + "; named series " + ", ".join(_markdown_code(metric) for metric in NAMED_METRICS)
        )
    else:
        metric_description = ", ".join(_markdown_code(metric) for metric in metrics)

    lines = [
        "# Measured Report Comparison",
        "",
        f"Baseline: {_markdown_code(baseline_path)}",
        f"Current: {_markdown_code(current_path)}",
        f"Metrics: {metric_description}",
        "Failure threshold: "
        f"> {_format_number(fail_regression_pct)}% regression "
        f"and > {_format_number(min_regression_delta)} absolute delta.",
        "",
        "## Summary",
        "",
        "| Item | Count |",
        "|------|------:|",
        f"| Compared metric values | {len(result.comparisons)} |",
        f"| Regressions over threshold | {len(result.regressions)} |",
        f"| Improvements | {len(result.improvements)} |",
        f"| Stable values | {stable_count} |",
        f"| New benchmarks | {len(result.new_cases)} |",
        f"| Missing benchmarks | {len(result.missing_cases)} |",
        f"| Omitted metric values | {len(result.omitted_metrics)} |",
        f"| Unavailable metric values | {len(result.unavailable_metrics)} |",
        "",
    ]
    if fail_on_new:
        lines.extend(["Policy: new benchmarks are failures.", ""])
    if fail_on_missing:
        lines.extend(["Policy: missing benchmarks are failures.", ""])

    _append_comparison_table(lines, "Regressions", result.regressions)
    _append_comparison_table(lines, "Improvements", result.improvements)
    _append_case_table(lines, "New Benchmarks", result.new_cases)
    _append_case_table(lines, "Missing Benchmarks", result.missing_cases)

    if result.omitted_metrics:
        lines.extend(["## Omitted Metrics", ""])
        for item in result.omitted_metrics:
            lines.append(f"- {_markdown_code(item)}")
        lines.append("")

    if result.unavailable_metrics:
        lines.extend(["## Unavailable Statistics", ""])
        for item in result.unavailable_metrics:
            lines.append(f"- {_markdown_code(item)}")
        lines.append("")

    return "\n".join(lines).rstrip() + "\n"


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", required=True, type=Path, help="Baseline Gentest measured JSON or CSV report.")
    parser.add_argument("--current", required=True, type=Path, help="Current Gentest measured JSON or CSV report.")
    parser.add_argument(
        "--metric",
        action="append",
        dest="metrics",
        help=f"Metric to compare for every row. May be repeated. Defaults: primary {', '.join(DEFAULT_METRICS)}; "
        f"named series {', '.join(NAMED_METRICS)}.",
    )
    parser.add_argument(
        "--fail-regression-pct",
        type=float,
        default=10.0,
        help="Fail when current value is greater than baseline by more than this percentage. Default: 10.",
    )
    parser.add_argument(
        "--min-regression-delta",
        type=float,
        default=0.0,
        help="Require this absolute positive delta before a percentage regression can fail. Default: 0.",
    )
    parser.add_argument("--fail-on-new", action="store_true", help="Treat benchmarks present only in the current report as failures.")
    parser.add_argument("--fail-on-missing", action="store_true", help="Treat benchmarks missing from the current report as failures.")
    parser.add_argument("--markdown-out", type=Path, help="Write the Markdown summary to this path instead of stdout.")
    args = parser.parse_args(argv)

    if not math.isfinite(args.fail_regression_pct) or args.fail_regression_pct < 0.0:
        parser.error("--fail-regression-pct must be finite and non-negative")
    if not math.isfinite(args.min_regression_delta) or args.min_regression_delta < 0.0:
        parser.error("--min-regression-delta must be finite and non-negative")
    args.metrics = _unique_metrics(args.metrics) if args.metrics else None
    return args


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        baseline = load_report(args.baseline)
        current = load_report(args.current)
        _require_cases("baseline", args.baseline, baseline)
        _require_cases("current", args.current, current)
        result = compare_reports(
            baseline,
            current,
            args.metrics,
            args.fail_regression_pct,
            args.min_regression_delta,
        )
        markdown = render_markdown(
            args.baseline,
            args.current,
            args.metrics,
            result,
            args.fail_regression_pct,
            args.min_regression_delta,
            args.fail_on_new,
            args.fail_on_missing,
        )
        if args.markdown_out:
            args.markdown_out.parent.mkdir(parents=True, exist_ok=True)
            args.markdown_out.write_text(markdown, encoding="utf-8")
        else:
            sys.stdout.write(markdown)

        failed = bool(result.regressions)
        failed = failed or (args.fail_on_new and bool(result.new_cases))
        failed = failed or (args.fail_on_missing and bool(result.missing_cases))
        return 1 if failed else 0
    except (OSError, ValueError, json.JSONDecodeError, csv.Error) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
