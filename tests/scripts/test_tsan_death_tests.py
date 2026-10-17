"""Check actual CTest metadata rather than a copied list of death-test names."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from check_tsan_death_tests import enabled_death_tests


class TSanDeathInventoryTests(unittest.TestCase):
    def test_enabled_and_disabled_metadata(self):
        self.assertEqual(enabled_death_tests({"tests": [{"name": "new death case"}]}), ["new death case"])
        with self.assertRaisesRegex(ValueError, "disabled: unseen death case"):
            enabled_death_tests({"tests": [{"name": "unseen death case", "properties": [{"name": "DISABLED", "value": True}]}]})

    def test_empty_and_malformed_inventories_fail_closed(self):
        for inventory in ({}, {"tests": []}, {"tests": None}, {"tests": [{}]},
                          {"tests": [{"name": "a", "properties": "bad"}]},
                          {"tests": [{"name": "a", "properties": [{"name": "DISABLED", "value": "FALSE"}]}]}):
            with self.subTest(inventory=inventory), self.assertRaises(ValueError):
                enabled_death_tests(inventory)

    def test_configured_inventory_rejects_a_real_disabled_death_test(self):
        cmake = shutil.which("cmake")
        self.assertIsNotNone(cmake, "CMake is required for the configured-inventory contract")
        ctest = str(Path(cmake).with_name("ctest.exe" if os.name == "nt" else "ctest"))
        configuration = "RelWithDebInfo" if os.name == "nt" else "Debug"
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.20)
project(death_inventory NONE)
include(CTest)
add_test(NAME actual_death_case COMMAND "${CMAKE_COMMAND}" --version)
set_tests_properties(actual_death_case PROPERTIES LABELS death DISABLED "${DISABLE_DEATH}")
add_test(NAME unrelated_case COMMAND "${CMAKE_COMMAND}" --version)
set_tests_properties(unrelated_case PROPERTIES DISABLED TRUE)
''')
            build = root / "build"
            command = [sys.executable, str(ROOT / "scripts/check_tsan_death_tests.py"), "--build-dir", str(build),
                       "--config", configuration, "--ctest", ctest]
            for disabled, expected in (("OFF", 0), ("ON", 1)):
                subprocess.run([cmake, "-S", str(root), "-B", str(build), "-DDISABLE_DEATH=" + disabled,
                                "-DCMAKE_BUILD_TYPE=" + configuration],
                               capture_output=True, text=True, check=True)
                result = subprocess.run(command, capture_output=True, text=True)
                self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
                if expected:
                    self.assertIn("actual_death_case", result.stderr)
                else:
                    self.assertIn("1 enabled death tests", result.stdout)


if __name__ == "__main__":
    unittest.main()
