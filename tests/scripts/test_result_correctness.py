"""Exercise runner results and the generated CTest protocol as a consumer."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

from target_command import target_command


def main():
    binary, helper, cmake, ctest = sys.argv[1:5]

    # Discovery and the CTest commands it emits must use the target's executor too.
    executor = ";".join(argument.replace(";", r"\;") for argument in target_command(binary)[:-1])

    def run(*args, expected=0):
        result = subprocess.run(target_command(binary, *args, "--no-color"), capture_output=True, text=True, timeout=30)
        assert result.returncode == expected, result
        return result.stdout + result.stderr

    run("--run=result_correctness/approx_special_values")
    for operation in range(9):
        text = run(f"--run=result_correctness/late_mock({operation})", expected=1)
        assert "Cannot configure mock" in text, text
        assert "before using" in text, text

    for name in ("runtime_skip", "static_skip"):
        run(f"--run=result_correctness/{name}")
        run(f"--run=result_correctness/{name}", "--skip-return-code=77", expected=77)
    run("--run=result_correctness/failure_then_skip", "--skip-return-code=77", expected=1)
    run("--run=result_correctness/skip_with_teardown_failure", "--skip-return-code=77", expected=1)
    for code in ("0", "1", "3", "256", "-1", "oops"):
        run("--skip-return-code=" + code, expected=1)

    with tempfile.TemporaryDirectory(prefix="gentest-results-") as directory:
        root = Path(directory)
        report = root / "results.xml"
        run("--run=result_correctness/pass", "--junit=" + str(report))
        assert ET.parse(report).getroot().attrib["tests"] == "1"
        for args in (("--filter=absent",), ("--filter=absent", "--kind=test"),
                     ("--kind=bench", "--exclude=*_mock", "--report-format=json"),
                     ("--filter=absent", "--report-format=json"),
                     ("--filter=*body_death",)):
            report.write_text("stale evidence")
            run(*args, "--junit=" + str(report))
            suite = ET.parse(report).getroot()
            assert suite.attrib == {"name": "gentest", "tests": "0", "failures": "0", "skipped": "0", "errors": "0"}, suite.attrib
            assert not list(suite), list(suite)
        # Mock verification inside a measured call reports a failure instead of terminating (#262).
        measured = {
            "bench_late_unmet_mock": "expected 1 call(s) to ::mocking::Calculator::reset but observed 0",
            "bench_require_unwinds_mock": "bench require unwinds mock",
            "bench_satisfied_mock": None,
            "bench_unmet_mock": "expected 1 call(s) to ::mocking::Calculator::reset but observed 0",
            "jitter_unmet_mock": "expected 1 call(s) to ::mocking::Calculator::reset but observed 0",
        }
        text = run("--filter=result_correctness/*_mock", "--bench-min-epoch-time-s=0.001", "--junit=" + str(report), expected=1)
        assert "terminate" not in text, text
        assert "Summary: passed 1/5; failed 4;" in text, text
        suite = ET.parse(report).getroot()
        assert suite.attrib["tests"] == "5" and suite.attrib["failures"] == "4", suite.attrib
        cases = {item.attrib["name"]: item for item in suite.iter("testcase")}
        for name, message in measured.items():
            failure = cases["result_correctness/" + name].find("failure")
            if message is None:
                assert failure is None, ET.tostring(failure)
            else:
                assert failure is not None and message in failure.text, (name, failure)
                assert message in text, (name, text)

        report.write_text("preview sentinel")
        run("--list-selected", "--filter=absent", "--junit=" + str(report))
        assert report.read_text() == "preview sentinel"
        run("--filter=absent", "--junit=" + str(root), expected=1)
        run("--run=result_correctness/runtime_skip", "--skip-return-code=77", "--junit=" + str(root), expected=1)

        # Use the same emitted discovery script and harness a downstream project uses.
        config = root / "configure.cmake"
        config.write_text(f'''cmake_minimum_required(VERSION 3.31)
set(CMAKE_BINARY_DIR [==[{root}]==])
include([==[{helper}]==])
_gentest_write_discover_tests_script(script)
file(WRITE [==[{root / 'script.txt'}]==] "${{script}}")
''')
        subprocess.run([cmake, "-P", str(config)], check=True, capture_output=True, text=True)
        discover = (root / "script.txt").read_text()

        def discovery(filename, pattern, extra="", working_directory=None):
            command = [cmake, "-DTEST_EXECUTABLE=" + binary, "-DTEST_WORKING_DIR=" + str(working_directory or directory),
                       "-DTEST_EXECUTOR=" + executor,
                       "-DCTEST_FILE=" + str(root / filename), "-DTEST_FILTER=" + pattern,
                       "-DTEST_EXTRA_ARGS=" + extra, "-DTEST_LIST=cases", "-P", discover]
            subprocess.run(command, check=True, capture_output=True, text=True)

        discovery("CTestTestfile.cmake", "result_correctness/*skip")
        result = subprocess.run([ctest, "--test-dir", directory, "--output-on-failure", "--output-junit", str(root / "ctest.xml")],
                                capture_output=True, text=True, timeout=30)
        assert result.returncode != 0, result  # failure_then_skip remains a failure.
        cases = {item.attrib["name"]: item for item in ET.parse(root / "ctest.xml").iter("testcase")}
        for name in ("runtime_skip", "static_skip"):
            assert cases["result_correctness/" + name].find("skipped") is not None, cases
        assert cases["result_correctness/failure_then_skip"].find("failure") is not None, cases

        for name, extra, expected, message in (
            ("body_death", "", 0, "Death test passed"),
            ("body_death_77", "", 0, "Death test passed"),
            ("runtime_skip_death", "", 0, "Death test skipped by test binary"),
            ("async_death", "", 0, "Death test passed"),
            ("body_death", "--definitely-invalid", 8, "did not reach the selected test body"),
            ("setup_death", "", 8, "did not reach the selected test body"),
            ("teardown_death", "", 8, "body completed without the expected termination"),
        ):
            discovery("CTestTestfile.cmake", "result_correctness/" + name, extra)
            result = subprocess.run([ctest, "--test-dir", directory, "-V"], capture_output=True, text=True, timeout=30)
            assert result.returncode == expected, result
            assert message in result.stdout + result.stderr, result
        readonly = root / "readonly-cwd"
        readonly.mkdir()
        os.chmod(readonly, 0o555)
        try:
            discovery("CTestTestfile.cmake", "result_correctness/body_death", working_directory=readonly)
            result = subprocess.run([ctest, "--test-dir", directory, "-V"], capture_output=True, text=True, timeout=30)
            assert result.returncode == 0 and "Death test passed" in result.stdout + result.stderr, result
        finally:
            os.chmod(readonly, 0o755)
        assert not list(root.rglob("gentest-death-*.started"))
    print("result correctness contract passed")


if __name__ == "__main__":
    main()
