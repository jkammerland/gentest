#!/usr/bin/env python3
"""Missing optional backend programs must produce real CTest skips."""

from __future__ import annotations

import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]


def cmake_atom(value):
    value = str(value)
    equals = "="
    while "]" + equals + "]" in value:
        equals += "="
    return "[" + equals + "[" + value + "]" + equals + "]"


class BackendPrerequisiteSkipTests(unittest.TestCase):
    def test_missing_backend_programs_are_reported_as_skipped(self):
        cmake, ctest = shutil.which("cmake"), shutil.which("ctest")
        self.assertIsNotNone(cmake)
        self.assertIsNotNone(ctest)

        # Discover registered execution checks that search for a backend executable.
        registration = (ROOT / "tests/CMakeLists.txt").read_text()
        helper = re.search(r"(?ms)^function\(_gentest_add_cmake_helper_test\b.*?^endfunction\(\)", registration)
        self.assertIsNotNone(helper)
        paths = set(re.findall(r"\$\{PROJECT_SOURCE_DIR\}/tests/cmake/scripts/(Check(?:Bazel|Meson|Xmake)\w+\.cmake)", registration))
        checks = {}
        tools = set()
        for name in sorted(paths):
            script = ROOT / "tests/cmake/scripts" / name
            match = re.search(r"find_program\(\w+\s+NAMES\s+(bazelisk|bazel|meson|xmake)\b", script.read_text())
            if match:
                checks["missing_" + script.stem] = script
                tools.add("bazel" if match[1] == "bazelisk" else match[1])
        self.assertEqual(tools, {"bazel", "meson", "xmake"})
        self.assertTrue(checks)

        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary)
            build = source / "build"
            lines = [
                "cmake_minimum_required(VERSION 3.31)",
                "project(backend_prerequisite_skips LANGUAGES NONE)",
                "enable_testing()",
                "set(_gentest_codegen_prog " + cmake_atom(sys.executable) + ")",
                helper[0],
            ]
            for name, script in checks.items():
                lines.append("_gentest_add_cmake_helper_test(" + name + " " + cmake_atom(ROOT) + " " + cmake_atom(script) + " contract\n"
                             "  -DCMAKE_FIND_USE_CMAKE_SYSTEM_PATH=OFF\n"
                             "  -DCMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH=OFF\n"
                             "  -DCMAKE_FIND_USE_CMAKE_ENVIRONMENT_PATH=OFF\n"
                             "  -DCMAKE_FIND_USE_CMAKE_PATH=OFF)")
            (source / "CMakeLists.txt").write_text("\n\n".join(lines) + "\n")

            # Explicit backend overrides must not make this absence control depend on the host.
            env = {key: value for key, value in os.environ.items() if key != "BAZEL_EXECUTABLE"}
            configured = subprocess.run([cmake, "-S", str(source), "-B", str(build), "-DCMAKE_TOOLCHAIN_FILE:FILEPATH="],
                                        env=env, capture_output=True, text=True)
            self.assertEqual(configured.returncode, 0, configured.stdout + configured.stderr)
            inventory = subprocess.run([ctest, "--test-dir", str(build), "--show-only=json-v1"],
                                       env=env, capture_output=True, text=True)
            self.assertEqual(inventory.returncode, 0, inventory.stdout + inventory.stderr)
            tests = json.loads(inventory.stdout)["tests"]
            self.assertEqual({test["name"] for test in tests}, set(checks))
            for test in tests:
                props = {prop["name"]: prop["value"] for prop in test["properties"]}
                self.assertIn("GENTEST_SKIP_TEST:", props.get("SKIP_REGULAR_EXPRESSION", []))

            result_file = build / "results.xml"
            ran = subprocess.run([ctest, "--test-dir", str(build), "--output-on-failure", "--no-tests=error",
                                  "--output-junit", str(result_file)], env=env, capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            results = ET.parse(result_file).findall("testcase")
            self.assertEqual({test.attrib["name"] for test in results}, set(checks))
            for test in results:
                with self.subTest(check=test.attrib["name"]):
                    self.assertIsNotNone(test.find("skipped"), ran.stdout + ran.stderr)
                    self.assertIn("GENTEST_SKIP_TEST:", test.findtext("system-out", ""))


if __name__ == "__main__":
    unittest.main()
