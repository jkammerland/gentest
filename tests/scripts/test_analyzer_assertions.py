#!/usr/bin/env python3

from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CLANG_TIDY = shutil.which("clang-tidy")
CHECK = "bugprone-unchecked-optional-access"
EXTRA_INCLUDE_DIRS = tuple(
    Path(entry) for entry in os.environ.get("GENTEST_ANALYZER_INCLUDE_DIRS", "").split("|") if entry
)
CONTROL_SOURCE = """
#include <optional>
int probe(std::optional<int> value) {
    return *value;
}
"""


@unittest.skipUnless(CLANG_TIDY, "clang-tidy is not available")
class AnalyzerAssertionTests(unittest.TestCase):
    control_result: subprocess.CompletedProcess[str]

    @classmethod
    def setUpClass(cls) -> None:
        cls.control_result = cls.run_tidy(CONTROL_SOURCE)

    @staticmethod
    def run_tidy(source: str) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory(prefix="gentest analyzer assertions ") as temporary:
            probe = Path(temporary) / "optional probe.cpp"
            probe.write_text(source, encoding="utf-8")
            return subprocess.run(
                [
                    CLANG_TIDY or "clang-tidy",
                    str(probe),
                    f"--checks=-*,{CHECK}",
                    "--",
                    "-std=c++20",
                    f"-I{ROOT / 'include'}",
                    *(f"-I{include_dir}" for include_dir in EXTRA_INCLUDE_DIRS),
                ],
                check=False,
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
            )

    def require_control_diagnostic(self) -> None:
        output = self.control_result.stdout + self.control_result.stderr
        self.assertEqual(self.control_result.returncode, 0, output)
        if f"[{CHECK}]" not in output:
            self.skipTest(f"{CHECK} does not model this platform's std::optional implementation")

    def test_control_probe_reports_unchecked_optional_access(self) -> None:
        self.require_control_diagnostic()

    def test_assert_true_prunes_failed_optional_path(self) -> None:
        self.require_control_diagnostic()
        result = self.run_tidy(
            """
#include "gentest/analyzer_assertions.h"
#include <optional>
int probe(std::optional<int> value) {
    ASSERT_TRUE(value.has_value(), "value is required");
    return *value;
}
"""
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn(f"[{CHECK}]", result.stdout + result.stderr)

    def test_assert_false_prunes_failed_optional_path(self) -> None:
        self.require_control_diagnostic()
        result = self.run_tidy(
            """
#include "gentest/analyzer_assertions.h"
#include <optional>
int probe(std::optional<int> value) {
    ASSERT_FALSE(!value.has_value());
    return *value;
}
"""
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn(f"[{CHECK}]", result.stdout + result.stderr)

    def test_existing_assertion_macro_is_rejected(self) -> None:
        result = self.run_tidy(
            """
#define ASSERT_TRUE(value) static_cast<void>(value)
#include "gentest/analyzer_assertions.h"
"""
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("requires ASSERT_TRUE and ASSERT_FALSE to be undefined", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
