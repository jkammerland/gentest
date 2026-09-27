#!/usr/bin/env python3

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / ".github" / "workflows" / "cmake.yml"
TESTS_CMAKE = ROOT / "tests" / "CMakeLists.txt"


class CiPackageLaneTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.workflow = WORKFLOW.read_text(encoding="utf-8")
        cls.tests_cmake = TESTS_CMAKE.read_text(encoding="utf-8")

    def test_linux_and_windows_default_package_override_is_still_forwarded(self):
        override = '\"-DGENTEST_ENABLE_PACKAGE_TESTS=${{ matrix.enable_package_tests || \'OFF\' }}\"'
        self.assertGreaterEqual(self.workflow.count(override), 2)

    def test_full_matrix_keeps_representative_package_consumers(self):
        import json
        matrix = json.loads((ROOT / "scripts" / "ci_matrix.json").read_text())
        gcc = [entry for entry in matrix["linux"]
               if entry["name"] == "Ubuntu 24.04 • GCC" and entry["build_type"] == "release"]
        self.assertEqual(len(gcc), 1)
        self.assertEqual(gcc[0]["enable_package_tests"], "ON")
        windows = [entry for entry in matrix["windows"]
                   if entry["llvm-version"] == "21.1.4" and entry["preset"] == "debug-system"]
        self.assertEqual(len(windows), 1)
        self.assertEqual(windows[0]["enable_package_tests"], "ON")

    def test_full_matrix_retains_llvm_23_on_each_host_os(self):
        import json
        matrix = json.loads((ROOT / "scripts" / "ci_matrix.json").read_text())
        for platform, key, version in (("macos", "compiler", "llvm@23"),
                                       ("windows", "llvm-version", "23.1.0"),
                                       ("linux", "clang_version", "23")):
            with self.subTest(platform=platform):
                selected = [entry for entry in matrix[platform] if entry.get(key) == version]
                self.assertEqual(len(selected), 1)
        self.assertIn("Setup LLVM 23 package dependencies", self.workflow)
        self.assertIn("zlib:x64-windows", self.workflow)
        self.assertIn("zstd:x64-windows", self.workflow)
        self.assertIn("libxml2:x64-windows", self.workflow)

    def test_llvm_23_dependency_and_compiler_setup_contracts_remain(self):
        self.assertIn('brew --prefix "${{ matrix.compiler }}"', self.workflow)
        self.assertIn("VCPKG_ROOT: ${{ runner.temp }}/gentest-vcpkg", self.workflow)
        self.assertIn('"-DCMAKE_PREFIX_PATH=$env:LLVM_DEPENDENCY_PREFIX"', self.workflow)
        self.assertIn('"CMAKE_PREFIX_PATH=$prefix"', self.workflow)
        self.assertIn('(Join-Path $prefix "bin")', self.workflow)
        self.assertIn("'Suites: llvm-toolchain-noble-${{ matrix.clang_version }}'", self.workflow)
        self.assertIn('test "$("${COMPILER_BIN}/clang" --version', self.workflow)

    def test_package_pr_selects_workflow_and_manifest_contracts(self) -> None:
        for test_name in (
            "gentest_package_workflow_preset",
            "gentest_vcpkg_manifest_metadata",
        ):
            registration = self.tests_cmake.index(f"    {test_name}\n")
            next_registration = self.tests_cmake.index("\n_gentest_add_cmake_helper_test(", registration)
            self.assertIn(
                f'set_property(TEST {test_name} APPEND PROPERTY LABELS "package")',
                self.tests_cmake[registration:next_registration],
            )


if __name__ == "__main__":
    unittest.main()
