"""Contract tests for primary and named measurement report comparison."""

import csv
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[2] / "scripts" / "compare_measured_reports.py"
SPEC = importlib.util.spec_from_file_location("compare_measured_reports", SCRIPT)
compare = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = compare
SPEC.loader.exec_module(compare)


def primary(name="work", kind="bench", **fields):
    return {"report": kind, "id": kind + ".summary", "rows": [{
        "benchmark": name, "suite": "suite", "median_ns_per_item": 10,
        "p95_ns_per_item": 12, **fields,
    }]}


def named(name="work", series="step", kind="bench", **fields):
    return {"report": kind, "id": kind + ".measurements", "rows": [{
        "benchmark": name, "suite": "suite", "series": series, "count": 2,
        "execution_mode": "calibrated", "time_source": "runner", "sampling_kind": "observation",
        "median_ns": 100, "p95_ns": 120, "stddev_ns": 20, **fields,
    }]}


def report_text(tables, format):
    if format == "json":
        return json.dumps({"report": "measured", "tables": tables})

    output = io.StringIO()
    writer = csv.writer(output)
    writer.writerow(["report", "table", "row", "field", "type", "value"])
    row_counts = {}
    for table in tables:
        table_key = (table["report"], table["id"])
        for row in table["rows"]:
            index = row_counts.get(table_key, 0)
            row_counts[table_key] = index + 1
            for field, value in row.items():
                type_name = "null" if value is None else "number" if isinstance(value, (float, int)) else "string"
                writer.writerow([table["report"], table["id"], index, field, type_name, "" if value is None else value])
    return output.getvalue()


class MeasurementComparisonTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="gentest-measurements-")
        self.addCleanup(self.directory.cleanup)

    def load(self, tables, format="json"):
        path = Path(self.directory.name) / ("report." + format)
        path.write_text(report_text(tables, format), encoding="utf-8")
        return compare.load_report(path)

    def comparison(self, baseline, current):
        return compare.compare_reports(baseline, current, None, 10, 0)

    def test_primary_and_named_metrics_are_separate_in_both_formats(self):
        for format in ("json", "csv"):
            with self.subTest(format=format):
                before = self.load([primary(), named()], format)
                after = self.load([primary(), named(median_ns=130)], format)
                result = self.comparison(before, after)
                self.assertEqual(len(result.comparisons), 5)
                self.assertEqual(len(result.regressions), 1)
                regression = result.regressions[0]
                self.assertEqual(regression.key, compare.CaseKey("bench", "work", "step"))
                self.assertEqual((regression.metric, regression.baseline, regression.current), ("median_ns", 100, 130))

    def test_structural_keys_preserve_delimiters_empty_names_and_unicode(self):
        tables = [primary("a/b"), named("a/b", "c"), named("a", "b/c"),
                  named("a/b", ""), named("a/b", "(primary)"), named("a/b", 'comma,quote"|\nλ')]
        for format in ("json", "csv"):
            with self.subTest(format=format):
                cases = self.load(tables, format)
                self.assertEqual(len(cases), len(tables))
                result = self.comparison(cases, cases)
                self.assertEqual(len(result.comparisons), 17)
                markdown = compare.render_markdown(Path("base"), Path("current"), None, result, 10, 0, False, False)
                self.assertIn("Unavailable metric values | 0", markdown)

    def test_incompatible_primary_semantics_are_input_errors(self):
        for format in ("json", "csv"):
            for field, value in (("execution_mode", "once"), ("time_source", "supplied"), ("sampling_kind", "single-call")):
                with self.subTest(format=format, field=field):
                    before = self.load([primary()], format)
                    after = self.load([primary(**{field: value})], format)
                    with self.assertRaisesRegex(ValueError, "incompatible " + field):
                        self.comparison(before, after)

    def test_named_rows_also_reject_incompatible_semantics(self):
        for field, value in (("execution_mode", "once"), ("time_source", "supplied"), ("sampling_kind", "per-call")):
            with self.subTest(field=field):
                before = self.load([named()])
                after = self.load([named(**{field: value})])
                with self.assertRaisesRegex(ValueError, "incompatible " + field):
                    self.comparison(before, after)

    def test_historical_primary_reports_use_the_original_semantics(self):
        for kind, sampling in (("bench", "epoch-average"), ("jitter", "per-call")):
            with self.subTest(kind=kind):
                before = self.load([primary(kind=kind)])
                after = self.load([primary(kind=kind, execution_mode="calibrated", time_source="runner", sampling_kind=sampling)])
                self.assertEqual(len(self.comparison(before, after).comparisons), 2)

        before = self.load([primary(kind="jitter", mode="batch", calls_per_sample=8)])
        after = self.load([primary(kind="jitter", sampling_kind="batch-average", calls_per_sample=8)])
        self.assertEqual(len(self.comparison(before, after).comparisons), 2)
        with self.assertRaisesRegex(ValueError, "incompatible sampling_kind"):
            self.comparison(before, self.load([primary(kind="jitter")]))

    def test_batch_average_primary_requires_matching_known_widths(self):
        for format in ("json", "csv"):
            with self.subTest(format=format):
                before = self.load([primary(kind="jitter", sampling_kind="batch-average", calls_per_sample=8,
                                            p95_ns_per_item=100, stddev_ns_per_item=40)], format)
                same = self.load([primary(kind="jitter", sampling_kind="batch-average", calls_per_sample=8,
                                          p95_ns_per_item=90, stddev_ns_per_item=30)], format)
                self.assertEqual(len(self.comparison(before, same).improvements), 2)

                # A larger average can suppress variance and tails without improving individual calls.
                wider = self.load([primary(kind="jitter", sampling_kind="batch-average", calls_per_sample=128,
                                           p95_ns_per_item=20, stddev_ns_per_item=4)], format)
                with self.assertRaisesRegex(ValueError, "incompatible calls_per_sample"):
                    self.comparison(before, wider)

                # Historical missing metadata cannot be filled with an invented batch width.
                for fields in ({"sampling_kind": "batch-average"}, {"sample_kind": "batch-average"}, {"mode": "batch"}):
                    with self.subTest(fields=fields):
                        with self.assertRaisesRegex(ValueError, "calls_per_sample unavailable"):
                            self.load([primary(kind="jitter", **fields)], format)

    def test_batch_width_is_exact_integer_metadata(self):
        for format in ("json", "csv"):
            with self.subTest(format=format):
                before = self.load([primary(kind="jitter", sampling_kind="batch-average", calls_per_sample=2**53)], format)
                after = self.load([primary(kind="jitter", sampling_kind="batch-average", calls_per_sample=2**53 + 1)], format)
                with self.assertRaisesRegex(ValueError, "incompatible calls_per_sample"):
                    self.comparison(before, after)
                for width in (0, 1, 1.5, -8):
                    with self.subTest(width=width):
                        with self.assertRaisesRegex(ValueError, "calls_per_sample"):
                            self.load([primary(kind="jitter", sampling_kind="batch-average", calls_per_sample=width)], format)

    def test_unavailable_named_statistics_remain_explicit(self):
        for format in ("json", "csv"):
            with self.subTest(format=format):
                empty = self.load([named(count=0, median_ns=None, p95_ns=None, stddev_ns=None)], format)
                measured = self.load([named()], format)
                for current in (empty, measured):
                    result = self.comparison(empty, current)
                    self.assertEqual(result.comparisons, [])
                    self.assertEqual(result.regressions, [])
                    self.assertEqual(len(result.unavailable_metrics), 3)
                    markdown = compare.render_markdown(Path("base"), Path("current"), None, result, 10, 0, False, False)
                    self.assertIn("## Unavailable Statistics", markdown)
                    self.assertIn("unavailable in baseline", markdown)

    def test_named_additions_and_removals_do_not_replace_primary(self):
        before = self.load([primary(), named(series="old|stage")])
        after = self.load([primary(), named(series="new\nstage")])
        result = self.comparison(before, after)
        self.assertEqual(result.missing_cases, [compare.CaseKey("bench", "work", "old|stage")])
        self.assertEqual(result.new_cases, [compare.CaseKey("bench", "work", "new\nstage")])
        self.assertEqual(len(result.comparisons), 2)
        markdown = compare.render_markdown(Path("base"), Path("current"), None, result, 10, 0, False, False)
        self.assertIn("old&#124;stage", markdown)
        self.assertIn("new<br>stage", markdown)

    def test_duplicate_named_rows_and_missing_series_are_rejected(self):
        for format in ("json", "csv"):
            with self.subTest(format=format):
                with self.assertRaisesRegex(ValueError, "duplicate measured row"):
                    self.load([named(), named()], format)
                invalid = named()
                del invalid["rows"][0]["series"]
                with self.assertRaisesRegex(ValueError, "string 'series'"):
                    self.load([invalid], format)


if __name__ == "__main__":
    unittest.main()
