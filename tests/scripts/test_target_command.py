#!/usr/bin/env python3
"""Verify native and emulated child execution, including CMake/CTest transport."""

from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from target_command import target_command

ROOT = Path(__file__).resolve().parents[2]
PAYLOAD = ["argument with spaces", "semi;colon", 'double"quote', r"back\slash", "", "λ"]
PROBE = "import json, sys; print(json.dumps(sys.argv[1:]))"


class TargetCommandTests(unittest.TestCase):
    def test_native_execution_preserves_arguments_and_status(self):
        with patch.dict(os.environ, {}, clear=True):
            result = subprocess.run(target_command(sys.executable, "-c", PROBE, *PAYLOAD), capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(result.stdout), PAYLOAD)
        with patch.dict(os.environ, {"GENTEST_TEST_EMULATOR_JSON": "[]"}):
            result = subprocess.run(target_command(sys.executable, "-c", "raise SystemExit(7)"), capture_output=True, text=True)
            self.assertEqual(result.returncode, 7)

    def test_prefix_executes_child_without_shell_splitting(self):
        with tempfile.TemporaryDirectory() as tmp:
            wrapper = Path(tmp) / "emulator with spaces.py"
            wrapper.write_text('''import subprocess, sys
assert sys.argv[1] == 'prefix ; " \\\\ λ', sys.argv
raise SystemExit(subprocess.call(sys.argv[2:]))
''')
            prefix = [sys.executable, str(wrapper), 'prefix ; " \\ λ']
            with patch.dict(os.environ, {"GENTEST_TEST_EMULATOR_JSON": json.dumps(prefix)}):
                result = subprocess.run(target_command(sys.executable, "-c", PROBE, *PAYLOAD), capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(json.loads(result.stdout), PAYLOAD)
                result = subprocess.run(target_command(sys.executable, "-c", "raise SystemExit(9)"), capture_output=True, text=True)
                self.assertEqual(result.returncode, 9, result.stderr)

    def test_invalid_prefix_does_not_fall_back_to_direct_execution(self):
        for raw in ("", "not-json", "null", '"qemu"', "{}", '[""]', "[null]", "[23]", '["\\u0000"]'):
            with self.subTest(raw=raw), patch.dict(os.environ, {"GENTEST_TEST_EMULATOR_JSON": raw}):
                with self.assertRaises(ValueError):
                    target_command(sys.executable, "-c", "raise SystemExit(0)")

    def test_cmake_transports_prefix_and_keeps_host_tools_native(self):
        cmake = shutil.which("cmake")
        self.assertIsNotNone(cmake, "CMake is required for its test-registration contract")
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp)
            special = source / "emulator tools;data"
            special.mkdir()
            record, wrapper = source / "record.jsonl", special / "wrapper.py"
            marker = 'prefix ; " \\ λ'
            wrapper.write_text(f'''import json, pathlib, subprocess, sys
with pathlib.Path({str(record)!r}).open("a", encoding="utf-8") as output:
    output.write(json.dumps(sys.argv[1:]) + "\\n")
assert sys.argv[1] == {marker!r}, sys.argv
raise SystemExit(subprocess.call(sys.argv[2:]))
''')
            driver = source / "driver.py"
            driver.write_text(f'''import json, subprocess, sys
sys.path.insert(0, {str(ROOT / 'tests/scripts')!r})
from target_command import target_command
result = subprocess.run(target_command(sys.argv[1], "-c", {PROBE!r}, *{PAYLOAD!r}), capture_output=True, text=True)
assert result.returncode == 0, result.stderr
assert json.loads(result.stdout) == {PAYLOAD!r}, result.stdout
''')
            def atom(value):
                return "[=[" + str(value).replace(";", r"\;") + "]=]"
            prefix = [sys.executable, str(wrapper), marker]
            project = '''cmake_minimum_required(VERSION 3.20)
project(target_command_contract NONE)
include(CTest)
include("@ROOT@/cmake/GentestTests.cmake")
set(Python3_EXECUTABLE @PYTHON@)
set(CMAKE_CROSSCOMPILING TRUE)
set(CMAKE_CROSSCOMPILING_EMULATOR @PREFIX@)
gentest_add_python_program_test(NAME emulated PROG @PYTHON@ SCRIPT @DRIVER@)
gentest_add_cmake_script_test(NAME host_cmake PROG "${CMAKE_COMMAND}"
    SCRIPT "@ROOT@/tests/cmake/scripts/CheckRunContract.cmake"
    ARGS -E echo "host cmake ran" DEFINES EXPECT_RC=0 "REQUIRED_SUBSTRING=host cmake ran")
gentest_add_cmake_script_test(NAME host_ctest PROG "${CMAKE_CTEST_COMMAND}"
    SCRIPT "@ROOT@/tests/cmake/scripts/CheckRunContract.cmake"
    ARGS --version DEFINES EXPECT_RC=0 "REQUIRED_SUBSTRING=ctest version")
gentest_add_cmake_script_test(NAME foreign_noemu NO_EMULATOR PROG foreign-program
    SCRIPT "@ROOT@/tests/cmake/cross_compile_emulator_serialization/ProbeScript.cmake")
gentest_add_cmake_script_test(NAME foreign_emulated PROG foreign-program
    SCRIPT "@ROOT@/tests/cmake/cross_compile_emulator_serialization/ProbeScript.cmake")
set(CMAKE_CROSSCOMPILING FALSE)
gentest_add_python_program_test(NAME native PROG @PYTHON@ SCRIPT @DRIVER@)
'''
            for key, value in (("@ROOT@", ROOT.as_posix()), ("@PYTHON@", atom(sys.executable)),
                               ("@PREFIX@", " ".join(map(atom, prefix))), ("@DRIVER@", atom(driver))):
                project = project.replace(key, value)
            (source / "CMakeLists.txt").write_text(project)
            build = source / "build"
            configure = subprocess.run([cmake, "-S", str(source), "-B", str(build)], capture_output=True, text=True)
            self.assertEqual(configure.returncode, 0, configure.stdout + configure.stderr)
            ctest = str(Path(cmake).with_name("ctest" + (".exe" if os.name == "nt" else "")))
            inventory = subprocess.run([ctest, "--test-dir", str(build), "--show-only=json-v1"], capture_output=True, text=True)
            self.assertEqual(inventory.returncode, 0, inventory.stderr)
            tests = {test["name"]: test for test in json.loads(inventory.stdout)["tests"]}
            for name in ("host_cmake", "host_ctest", "foreign_noemu"):
                with self.subTest(name=name):
                    self.assertFalse(any(arg.startswith("-DEMU") for arg in tests[name]["command"]))
            self.assertTrue(any(arg.startswith("-DEMU:STRING=") for arg in tests["foreign_emulated"]["command"]))
            # Ambient configuration must not leak into a registered native test.
            env = {**os.environ, "GENTEST_TEST_EMULATOR_JSON": "invalid ambient prefix"}
            result = subprocess.run([ctest, "--test-dir", str(build), "--output-on-failure", "--parallel", "1"],
                                    env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            calls = [json.loads(line) for line in record.read_text().splitlines()]
            self.assertEqual(calls, [[marker, sys.executable, "-c", PROBE, *PAYLOAD]])


if __name__ == "__main__":
    unittest.main()
