"""Inspect independent failure payloads, avoiding duplicated console summaries."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

from target_command import target_command


def main():
    binary = sys.argv[1]
    with tempfile.TemporaryDirectory(prefix="gentest-diagnostics-") as directory:
        report = Path(directory) / "report.xml"

        def run(name, expected=1):
            report.unlink(missing_ok=True)
            result = subprocess.run(target_command(binary, "--run=diagnostics_tests/" + name, "--no-color", "--junit=" + str(report)),
                                    capture_output=True, text=True, timeout=30)
            assert result.returncode == expected, result
            suite = ET.parse(report).getroot()
            cases = suite.findall("testcase")
            assert len(cases) == 1 and cases[0].attrib["name"] == "diagnostics_tests/" + name, cases
            failures = [item.text or "" for item in suite.iter("failure")]
            assert bool(failures) == bool(expected), failures
            return failures, result.stdout + result.stderr

        failures, _ = run("container_failure")
        assert len(failures) == 1, failures
        assert "lhs=[1, 2, 3], rhs=[1, 9, 3]" in failures[0], failures
        assert "first difference at index 1: 2 versus 9" in failures[0], failures

        failures, _ = run("string_failure")
        assert 'lhs="a\\nb\\0", rhs="a\\nc\\0"' in failures[0], failures
        assert "line 2, column 1 (byte 2)" in failures[0], failures

        # Character arrays end at their first NUL instead of exposing padding.
        failures, _ = run("padded_array_failure")
        assert 'lhs="ab", rhs="ac"' in failures[0], failures
        assert "\\0" not in failures[0], failures
        run("padded_array_matcher", expected=0)

        # Equal contents behind distinct pointers explain address comparison.
        failures, _ = run("distinct_pointer_failure")
        assert 'lhs="same", rhs="same"' in failures[0], failures
        assert "distinct pointers to equal strings" in failures[0], failures

        failures, _ = run("scoped_failure")
        assert len(failures) == 2, failures
        assert "element[1]" in failures[0] and "expected == 2, got 9" in failures[0], failures
        assert "packet=41" in failures[0] and "attempt=3" in failures[0], failures
        assert failures[0].index("packet=41") < failures[0].index("attempt=3"), failures
        assert "packet=41" in failures[1] and "attempt=3" not in failures[1], failures
        assert "cases.cpp:" in failures[0], failures

        failures, _ = run("scope_unwinding")
        assert "retired context" not in failures[0], failures
        failures, _ = run("fatal_matcher")
        assert len(failures) == 1 and "ASSERT_THAT" in failures[0] and "fatal context" in failures[0], failures
        assert "fatal matcher continued" not in failures[0], failures

        failures, _ = run("async_contexts")
        assert len(failures) == 3, failures
        for child in (1, 2):
            matches = [failure for failure in failures if f"child {child} assertion" in failure]
            assert len(matches) == 1, failures
            assert "request=7" in matches[0] and f"child={child}" in matches[0], failures
            assert f"child={3 - child}" not in matches[0], failures
        parent = [failure for failure in failures if "parent assertion" in failure]
        assert len(parent) == 1 and "request=7" in parent[0] and "child=" not in parent[0], failures

        failures, _ = run("cancellation_contexts")
        assert len(failures) == 2, failures
        assert "canceled child destructor" in failures[0] and "request=7" in failures[0] and "loser=1" in failures[0], failures
        assert "parent after cancellation" in failures[1] and "request=7" in failures[1] and "loser=1" not in failures[1], failures

        for name in ("matcher_success", "context_success", "async_context_success"):
            _, text = run(name, expected=0)
            assert "success-only" not in text and "context:" not in text, text

        failures, _ = run("native_elements")
        assert len(failures) == 1 and "element[1]" in failures[0], failures
        failures, _ = run("matcher_single_evaluation")
        assert len(failures) == 3 and all("single evaluation mismatch" in failure for failure in failures), failures
        for mode, diagnostic in enumerate(("unexpected element at index 0", "missing element at index 0", "element[0]: element[1]")):
            failures, _ = run(f"matcher_failures({mode})")
            assert len(failures) == 1 and diagnostic in failures[0], failures
        for mode in range(6):
            failures, text = run(f"mock_source({mode})")
            assert len(failures) == 1, failures
            declared = re.search(r"declared line (\d+)", text)
            assert declared, text
            if mode != 3:  # A rejected new expectation points to its own declaration.
                assert f"expectation at tests/diagnostics/cases.cpp:{declared[1]}" in failures[0], failures
            else:
                assert re.search(r"expectation at tests/diagnostics/cases.cpp:\d+", failures[0]), failures
    print("failure diagnostic contracts passed")


if __name__ == "__main__":
    main()
