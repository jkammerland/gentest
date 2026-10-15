"""Exercise generated public contracts, including intentionally failing occurrences."""
import json
import math
import subprocess
import sys


def run(binary, name, success=True, *extra):
    result = subprocess.run(
        [binary, "--filter=" + name, "--report-format=json", "--bench-epochs=2",
         "--bench-warmup=3", "--bench-min-total-time-s=0", "--bench-min-epoch-time-s=0", *extra],
        capture_output=True, text=True, timeout=30,
    )
    assert result.returncode == (0 if success else 1), (name, result.stdout, result.stderr)
    return json.loads(result.stdout), result.stderr


def main():
    binary = sys.argv[1]
    manual = subprocess.run([binary, "--manual-invalid"], capture_output=True, text=True, timeout=30)
    assert manual.returncode == 0, (manual.stdout, manual.stderr)
    primary, _ = run(binary, "*primary/*", True, "--repeat=3")
    tables = {table["id"]: table for table in primary["tables"]}
    assert len(tables) == len(primary["tables"]), primary

    # Do not let dictionary construction hide repeated complete benchmark lifecycles.
    for table_id in ("bench.summary", "bench.debug"):
        names = [row["benchmark"] for row in tables[table_id]["rows"]]
        assert len(names) == len(set(names)), (table_id, names)

    rows = {row["benchmark"].split("/")[-1]: row for row in tables["bench.summary"]["rows"]}
    debug_rows = {row["benchmark"].split("/")[-1]: row for row in tables["bench.debug"]["rows"]}
    assert {"call", "teardown", "static_items", "runner"} <= rows.keys(), rows
    for name in ("call", "teardown"):
        assert rows[name]["items_per_call"] == 1000, rows[name]
        assert rows[name]["median_ns_per_item"] == 2e6, rows[name]
        assert debug_rows[name]["items_per_sec"] == 500, debug_rows[name]
    assert rows["static_items"]["items_per_call"] == 16

    callback_rows = tables["jitter.measurements"]["rows"]
    assert len(callback_rows) == 1, callback_rows
    assert callback_rows[0]["series"] == "callback", callback_rows
    assert math.isclose(callback_rows[0]["p99_ns"], 1.99, rel_tol=0, abs_tol=1e-12), callback_rows

    # Run another process and select one case: no accidental dependence on earlier publications.
    series_report, _ = run(binary, "*series/valid")
    series_tables = {table["id"]: table for table in series_report["tables"]}
    series_rows = series_tables["bench.measurements"]["rows"]
    series = {row["series"]: row for row in series_rows}
    assert len(series) == len(series_rows), series_rows
    assert series["cpu"]["count"] == 5, series["cpu"]
    assert math.isclose(series["cpu"]["p99_ns"], 4.96, rel_tol=0, abs_tol=1e-12), series["cpu"]
    assert series["unavailable"]["count"] == 0 and series["unavailable"]["p99_ns"] is None, series["unavailable"]
    assert series["singleton_us"]["count"] == 1 and series["singleton_us"]["p99_ns"] == 7000, series["singleton_us"]
    assert math.isclose(series["microseconds"]["p99_ns"], 4960, rel_tol=0, abs_tol=1e-9), series["microseconds"]

    failures = {
        "missing": "duration was not supplied",
        "duplicate": "duration was already supplied",
        "zero_duration": "must be positive",
        "zero_items": "must be positive",
        "nan": "must be positive",
        "negative_duration": "must be positive",
        "infinite_duration": "must be positive",
        "overflow_duration": "must be positive",
        "runner_supplied": "once/supplied measured call",
        "series_negative": "nonnegative",
        "series_duplicate": "already published",
        "series_throw": "iteration or allocation failed",
        "series_phase": "local benchmark/jitter teardown",
        "series_overflow": "representable",
        "series_unit": "unit must be positive",
        "series_nonfinite": "representable",
        "series_name": "name must not be empty",
        "series_underflow": "representable",
        "series_allocation": "iteration or allocation failed",
        "series_summary_allocation": "summary allocation failed",
        "teardown_failure": "EXPECT_TRUE",
    }
    for name, diagnostic in failures.items():
        report, stderr = run(binary, "*invalid/" + name, False)
        assert diagnostic in stderr or diagnostic in json.dumps(report), (name, report, stderr)
        # Assertions in failing-case teardown are independent probes, not acceptable extra failures.
        # Otherwise an expected API rejection could mask broken atomicity/preflight/lifetime checks.
        if name != "teardown_failure":
            assert "EXPECT_" not in stderr and "ASSERT_" not in stderr, (name, stderr)
            assert "EXPECT_" not in json.dumps(report) and "ASSERT_" not in json.dumps(report), (name, report)
    print("benchmark measurement execution and publication contracts passed")


if __name__ == "__main__":
    main()
