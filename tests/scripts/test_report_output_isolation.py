"""Check report ownership using actual CTest commands emitted by Gentest helpers."""

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from check_allure_output_isolation import check_inventory


ROOT = Path(__file__).resolve().parents[2]


class ReportOutputIsolationTests(unittest.TestCase):
    def configure(self, root, second_directory):
        cmake, ctest = shutil.which("cmake"), shutil.which("ctest")
        self.assertIsNotNone(cmake)
        self.assertIsNotNone(ctest)
        source, build = root / "source", root / "build"
        source.mkdir()
        (source / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.20)
project(report_output_isolation LANGUAGES NONE)
enable_testing()
set(PROJECT_SOURCE_DIR "{ROOT.as_posix()}")
include("{ROOT.as_posix()}/cmake/GentestTests.cmake")
gentest_add_run_and_check_file(NAME status PROG "${{CMAKE_COMMAND}}"
  FILE "${{CMAKE_BINARY_DIR}}/status directory/result.json" REQUIRED_SUBSTRING "failed"
  ARGS "--allure-dir=${{CMAKE_BINARY_DIR}}/status directory")
gentest_add_cmake_script_test(NAME timing PROG "${{CMAKE_COMMAND}}"
  SCRIPT "{ROOT.as_posix()}/cmake/RunAndCheckReportTime.cmake"
  ARGS --kind=jitter "--allure-dir=${{CMAKE_BINARY_DIR}}/{second_directory}")
''', encoding="utf-8")
        result = subprocess.run([cmake, "-S", str(source), "-B", str(build)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return [sys.executable, str(ROOT / "tests/scripts/check_allure_output_isolation.py"),
                "--ctest", ctest, "--build-dir", str(build)]

    def test_real_helpers_keep_independent_report_directories(self):
        with tempfile.TemporaryDirectory() as temp:
            command = self.configure(Path(temp), "timing directory")
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("2 independent directories", result.stdout)

    def test_real_helpers_reject_shared_directory_with_different_path_spelling(self):
        with tempfile.TemporaryDirectory() as temp:
            command = self.configure(Path(temp), "unused/../status directory")
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("shared by status and timing", result.stderr)

    def test_empty_inventory_and_inventory_without_writers_are_rejected(self):
        for inventory in ([], [{"name": "unrelated", "command": ["runner", "--list"]}]):
            with self.subTest(inventory=inventory), self.assertRaisesRegex(ValueError, "No Allure"):
                check_inventory(inventory)


if __name__ == "__main__":
    unittest.main()
