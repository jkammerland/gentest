#!/usr/bin/env python3

from __future__ import annotations

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CLANG_TIDY = shutil.which("clang-tidy")
CHECK = "bugprone-unchecked-optional-access"


@unittest.skipUnless(CLANG_TIDY, "clang-tidy is not available")
class AnalyzerAssertionTests(unittest.TestCase):
    def run_tidy(self, source: str) -> subprocess.CompletedProcess[str]:
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
                ],
                check=False,
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
            )

    def test_control_probe_reports_unchecked_optional_access(self) -> None:
        result = self.run_tidy(
            """
#include <optional>
int probe(std::optional<int> value) {
    return *value;
}
"""
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(f"[{CHECK}]", result.stdout + result.stderr)

    def test_assert_true_prunes_failed_optional_path(self) -> None:
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
