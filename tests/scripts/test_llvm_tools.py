"""Exercise tool selection and fail-fast rejection without ambient LLVM installs."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import llvm_tools
from llvm_tool_fixture import fake_toolchain


@unittest.skipIf(os.name == "nt", "Fake executables use POSIX shell scripts")
class ToolSelectionTests(unittest.TestCase):
    def fake_tool(self, directory: Path, name: str, version: str) -> Path:
        executable = directory / name
        executable.write_text(f"#!/bin/sh\nprintf '%s\\n' '{version}'\n")
        executable.chmod(0o755)
        return executable

    def test_recognizes_compiler_analysis_and_llvm_config_versions(self):
        major = llvm_tools.LLVM_MAJOR
        for version in (f"Ubuntu clang version {major}.1.0", f"Debian LLVM version {major}.0.0",
                        f"clang-format version {major}.1.0", f"{major}.1.0"):
            with self.subTest(version=version), tempfile.TemporaryDirectory() as temp:
                tool = self.fake_tool(Path(temp), "tool", version)
                self.assertEqual(llvm_tools.check_tool(str(tool)), str(tool))

    def test_prefers_versioned_tool_over_an_old_path_default(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            self.fake_tool(directory, "clang-tidy", "LLVM version 21.1.8")
            expected = self.fake_tool(directory, f"clang-tidy-{llvm_tools.LLVM_MAJOR}",
                                      f"LLVM version {llvm_tools.LLVM_MAJOR}.1.0")
            with patch.dict(os.environ, {"PATH": temp}, clear=True):
                self.assertEqual(llvm_tools.resolve_tool("clang-tidy"), str(expected))

    def test_accepts_matching_unversioned_tool(self):
        with tempfile.TemporaryDirectory() as temp:
            tool = self.fake_tool(Path(temp), "clang-tidy", f"LLVM version {llvm_tools.LLVM_MAJOR}.1.0")
            with patch.dict(os.environ, {"PATH": temp}, clear=True):
                self.assertEqual(llvm_tools.resolve_tool("clang-tidy"), str(tool))

    def test_rejects_old_unknown_and_missing_tools(self):
        for version in ("LLVM version 21.1.8", "LLVM version 22.1.0", "unknown version"):
            with self.subTest(version=version), tempfile.TemporaryDirectory() as temp:
                tool = self.fake_tool(Path(temp), "clang-tidy", version)
                with patch.dict(os.environ, {"PATH": temp}, clear=True):
                    with self.assertRaisesRegex(ValueError, f"Expected LLVM {llvm_tools.LLVM_MAJOR}"):
                        llvm_tools.resolve_tool("clang-tidy")
                    with self.assertRaises(ValueError):
                        llvm_tools.resolve_tool("clang-format")

    def test_invalid_override_and_prefix_cannot_fall_back_to_a_good_tool(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            old = self.fake_tool(directory, "old-tidy", "LLVM version 21.1.8")
            self.fake_tool(directory, f"clang-tidy-{llvm_tools.LLVM_MAJOR}",
                           f"LLVM version {llvm_tools.LLVM_MAJOR}.1.0")
            with patch.dict(os.environ, {"PATH": temp}, clear=True):
                with self.assertRaisesRegex(ValueError, "Expected LLVM"):
                    llvm_tools.resolve_tool("clang-tidy", str(old))
                with patch.dict(os.environ, {"GENTEST_LLVM_PREFIX": str(directory / "missing")}):
                    with self.assertRaises(ValueError):
                        llvm_tools.resolve_tool("clang-tidy")

    def test_lint_scripts_reject_old_override_before_work(self):
        for script, variable in (("check_clang_tidy.sh", "CLANG_TIDY_BIN"),
                                 ("check_clang_format.sh", "CLANG_FORMAT_BIN")):
            with self.subTest(script=script), tempfile.TemporaryDirectory() as temp:
                old = self.fake_tool(Path(temp), "old-tool", "LLVM version 21.1.8")
                env = os.environ | {variable: str(old)}
                env.pop("GENTEST_LLVM_PREFIX", None)
                result = subprocess.run(["bash", str(ROOT / "scripts" / script), str(Path(temp) / "missing-build")],
                                        env=env, text=True, capture_output=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(f"Expected LLVM {llvm_tools.LLVM_MAJOR}", result.stderr)
                self.assertNotIn("compile_commands.json", result.stderr)

    def test_complete_selection_keeps_symlinked_tools_packages_and_overrides(self):
        with tempfile.TemporaryDirectory() as temp:
            prefix = fake_toolchain(Path(temp) / "llvm", llvm_tools.LLVM_MAJOR)
            link = Path(temp) / "current"
            link.symlink_to(prefix, target_is_directory=True)
            override = self.fake_tool(Path(temp), "custom-cov", f"LLVM version {llvm_tools.LLVM_MAJOR}.1.0")
            with patch.dict(os.environ, {"PATH": str(link / "bin"), "GENTEST_LLVM_PREFIX": str(link),
                                         "GENTEST_LLVM_COV": str(override)}, clear=True):
                selected = llvm_tools.select_toolchain()
            self.assertEqual(selected.tools["clang++"], str(prefix / "bin/clang++"))
            self.assertEqual(selected.tools["clang-scan-deps"], str(prefix / "bin/clang-scan-deps"))
            self.assertEqual(selected.tools["llvm-cov"], str(override))
            self.assertEqual(selected.tools["llvm-config"], str(prefix / "bin/llvm-config"))
            self.assertEqual(selected.llvm_dir, str(prefix / "lib/cmake/llvm"))
            self.assertEqual(selected.clang_dir, str(prefix / "lib/cmake/clang"))

    def test_package_major_mismatch_and_stale_explicit_packages_fail(self):
        with tempfile.TemporaryDirectory() as temp:
            prefix = fake_toolchain(Path(temp) / "llvm", llvm_tools.LLVM_MAJOR)
            stale = fake_toolchain(Path(temp) / "stale", 21)
            env = {"PATH": str(prefix / "bin"), "GENTEST_LLVM_PREFIX": str(prefix)}
            for overrides, expected in (({"Clang_DIR": str(stale / "lib/cmake/clang")}, "must agree"),
                                        ({"LLVM_DIR": str(stale / "lib/cmake/llvm"),
                                          "Clang_DIR": str(stale / "lib/cmake/clang")}, "Expected LLVM/Clang")):
                with self.subTest(overrides=overrides), patch.dict(os.environ, env | overrides, clear=True):
                    with self.assertRaisesRegex(ValueError, expected):
                        llvm_tools.select_toolchain()

    def test_cmake_rejects_mismatched_host_packages_independently_of_target_compiler(self):
        cmake = shutil.which("cmake")
        self.assertIsNotNone(cmake, "CMake is required for the repository tooling checks")
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            llvm = fake_toolchain(root / "llvm", 23)
            clang = fake_toolchain(root / "clang", 22)
            (llvm / "lib/cmake/llvm/LLVMConfig.cmake").write_text("set(LLVM_VERSION_MAJOR 23)\n")
            (clang / "lib/cmake/clang/ClangConfig.cmake").write_text("set(CLANG_VERSION_MAJOR 22)\n")
            (root / "CMakeLists.txt").write_text(
                f'cmake_minimum_required(VERSION 3.25)\nproject(package_check LANGUAGES NONE)\n'
                f'include("{ROOT.as_posix()}/tools/CMakeLists.txt")\n')
            result = subprocess.run([cmake, "-S", str(root), "-B", str(root / "build"),
                                     f"-DLLVM_DIR={llvm / 'lib/cmake/llvm'}", f"-DClang_DIR={clang / 'lib/cmake/clang'}"],
                                    text=True, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertRegex(result.stderr, r"matching LLVM and Clang package majors; found LLVM 23,\s+Clang 22")

    def test_missing_or_stale_selected_scan_deps_and_cov_cannot_fall_back(self):
        with tempfile.TemporaryDirectory() as temp:
            prefix = fake_toolchain(Path(temp) / "llvm", llvm_tools.LLVM_MAJOR)
            other = fake_toolchain(Path(temp) / "other", llvm_tools.LLVM_MAJOR)
            old = self.fake_tool(Path(temp), "old", "LLVM version 21.1.0")
            env = {"PATH": str(other / "bin"), "GENTEST_LLVM_PREFIX": str(prefix)}
            for name, variable in (("clang-scan-deps", "GENTEST_CODEGEN_CLANG_SCAN_DEPS"),
                                   ("llvm-cov", "GENTEST_LLVM_COV")):
                with self.subTest(tool=name), patch.dict(os.environ, env | {variable: str(old)}, clear=True):
                    with self.assertRaisesRegex(ValueError, "Expected LLVM"):
                        llvm_tools.select_toolchain()
                with patch.dict(os.environ, env, clear=True):
                    selected = prefix / "bin" / name
                    selected.rename(selected.with_suffix(".saved"))
                    try:
                        with self.assertRaisesRegex(ValueError, "not found"):
                            llvm_tools.select_toolchain()
                    finally:
                        selected.with_suffix(".saved").rename(selected)

    def test_compiler_symlinks_infer_the_installation_and_explicit_host_override_is_validated(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            prefix = fake_toolchain(root / "llvm", llvm_tools.LLVM_MAJOR)
            public = root / "public"
            public.mkdir()
            (public / f"clang++-{llvm_tools.LLVM_MAJOR}").symlink_to(prefix / "bin/clang++")
            with patch.dict(os.environ, {"PATH": str(public)}, clear=True):
                self.assertEqual(llvm_tools.select_toolchain().tools["clang"], str(prefix / "bin/clang"))
            old = self.fake_tool(root, "old-host", "clang version 21.1.0")
            with patch.dict(os.environ, {"PATH": str(public), "GENTEST_CODEGEN_HOST_CLANG": str(old)}, clear=True):
                with self.assertRaisesRegex(ValueError, "Expected LLVM"):
                    llvm_tools.select_toolchain()


if __name__ == "__main__":
    unittest.main()
