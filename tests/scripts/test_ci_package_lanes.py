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

    def test_linux_and_windows_matrices_disable_package_tests_by_default(self) -> None:
        override = (
            '"-DGENTEST_ENABLE_PACKAGE_TESTS=' "${{ matrix.enable_package_tests || 'OFF' }}" '"'
        )
        self.assertGreaterEqual(self.workflow.count(override), 2)

    def test_representative_gcc_and_windows_lanes_keep_package_consumers(self) -> None:
        gcc_debug = self.workflow.index("- name: Ubuntu 24.04 • GCC")
        gcc_release = self.workflow.index("- name: Ubuntu 24.04 • GCC", gcc_debug + 1)
        next_entry = self.workflow.index("- name:", gcc_release + 1)
        self.assertIn('enable_package_tests: "ON"', self.workflow[gcc_release:next_entry])

        windows_matrix = self.workflow.index("# Keep one Windows LLVM/MSVC job")
        windows_steps = self.workflow.index("    steps:", windows_matrix)
        representative = self.workflow[windows_matrix:windows_steps]
        self.assertIn('llvm-version: "21.1.4"', representative)
        self.assertIn('preset: "debug-system"', representative)
        self.assertIn('enable_package_tests: "ON"', representative)

    def test_llvm_23_has_one_focused_lane_on_each_host_os(self) -> None:
        self.assertIn('compiler: ["appleclang", "llvm@20", "llvm@21", "llvm@23"]', self.workflow)
        self.assertIn("brew --prefix \"${{ matrix.compiler }}\"", self.workflow)
        self.assertIn(
            'compiler: "llvm@23"\n                build-type: "release"\n                variant: "default"',
            self.workflow,
        )
        self.assertIn('llvm-version: ["23.1.0", "22.1.0", "21.1.4"]', self.workflow)
        self.assertIn(
            'llvm-version: "23.1.0"\n            preset: "release-system"\n            variant: "default"',
            self.workflow,
        )

        self.assertEqual(self.workflow.count("- name: Ubuntu 24.04 • LLVM 23"), 1)
        llvm_23_linux = self.workflow.index("- name: Ubuntu 24.04 • LLVM 23")
        next_entry = self.workflow.index("- name:", llvm_23_linux + 1)
        linux_lane = self.workflow[llvm_23_linux:next_entry]
        self.assertIn('clang_version: "23"', linux_lane)
        self.assertIn("build_type: debug", linux_lane)
        self.assertIn("ci_exhaustive: true", linux_lane)

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
