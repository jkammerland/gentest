"""Exercise tool selection and fail-fast rejection without ambient LLVM installs."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import llvm_tools


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


if __name__ == "__main__":
    unittest.main()
