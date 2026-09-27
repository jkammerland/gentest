"""Exercise the discovery harness, including its Windows Debug sub-probe gate."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class DiscoverySkipTests(unittest.TestCase):
    def test_only_windows_debug_with_the_switch_skips_abort(self):
        # WIN32 is overridden only in the script interpreter. The tiny fixture
        # still compiles for the real host, so this checks the gate on every OS.
        cases = [(True, "Debug", True), (False, "Debug", True),
                 (True, "Release", True), (True, "Debug", False)]
        if os.name == "nt":
            # Do not run the known debugger-attachment hang to test its gate.
            cases = [(True, "Debug", True), (True, "Release", True)]
        for windows, config, switch in cases:
            with self.subTest(windows=windows, config=config, switch=switch), tempfile.TemporaryDirectory() as tmp:
                result = subprocess.run([
                    "cmake", f"-DWIN32={'TRUE' if windows else 'FALSE'}",
                    f"-DSOURCE_DIR={ROOT / 'tests/cmake/discover_tests'}", f"-DBUILD_ROOT={tmp}",
                    "-DGENERATOR=Ninja", f"-DBUILD_TYPE={config}",
                    f"-DGENTEST_SKIP_WINDOWS_DEBUG_DEATH_TESTS={'ON' if switch else 'OFF'}",
                    "-P", str(ROOT / "tests/cmake/scripts/CheckDiscoverTests.cmake"),
                ], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
                self.assertEqual(result.returncode, 0, result.stdout)
                skipped = windows and config == "Debug" and switch
                self.assertEqual("GENTEST_KNOWN_SKIP: windows-debug-abort:" in result.stdout, skipped)
                self.assertEqual("Run aborting death-harness probe..." in result.stdout, not skipped)
                self.assertIn("Run discovered tests...", result.stdout)
                self.assertIn("Run discovered death tests...", result.stdout)
                self.assertIn("gentest_discover_tests fixture passed", result.stdout)


if __name__ == "__main__":
    unittest.main()
