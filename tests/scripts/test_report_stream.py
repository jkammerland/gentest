"""Check complete machine reports and independently retained diagnostics."""
import csv
import io
import json
import subprocess
import sys


def main():
    binary = sys.argv[1]
    markers = ("shared setup marker", "local setup marker", "body marker", "worker marker",
               "local teardown marker", "shared teardown marker")
    for fmt in ("json", "csv"):
        for case in ("success", "failure"):
            for disabled in (False, True):
                args = [binary, "--run=report_stream/" + case,
                        "--report-format=" + fmt, "--no-color"]
                if disabled:
                    args.append("--without-default-sink")
                result = subprocess.run(args, capture_output=True, text=True, timeout=30)
                assert result.returncode == (0 if case == "success" else 1), result
                if fmt == "json":
                    report = json.loads(result.stdout)
                    assert bool(report["issues"]) == (case == "failure"), report
                else:
                    rows = list(csv.reader(io.StringIO(result.stdout)))
                    assert rows[0] == ["report", "table", "row", "field", "type", "value"], rows
                    assert len(rows) > 1 and all(len(row) == 6 for row in rows), rows
                for marker in markers:
                    assert marker not in result.stdout, result.stdout
                    assert (marker in result.stderr) != disabled, result.stderr
                if case == "failure":
                    assert "deliberate teardown failure" in result.stderr, result.stderr

    for case in ("success", "failure"):
        result = subprocess.run([binary, "--run=report_stream/" + case, "--report-format=json",
                                 "--verify-restoration", "--no-color"],
                                capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, result
        report, end = json.JSONDecoder().raw_decode(result.stdout.lstrip())
        assert bool(report["issues"]) == (case == "failure"), report
        assert "ordinary marker" in result.stdout.lstrip()[end:], result.stdout
        assert "ordinary marker" not in result.stderr, result.stderr
    print("machine report logging and restoration passed")


if __name__ == "__main__":
    main()
