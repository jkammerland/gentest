#!/usr/bin/env python3
"""Behavior checks for hosted profile selection."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from ci_matrix import select_matrix


class MatrixTests(unittest.TestCase):
    def test_routine_matrix_has_only_selected_environments(self):
        selected = select_matrix("pr")
        self.assertEqual({host: len(matrix["include"]) for host, matrix in selected.items()},
                         {"linux": 3, "windows": 1, "macos": 1})
        self.assertEqual({(job["name"], job["build_type"]) for job in selected["linux"]["include"]},
                         {("Ubuntu 25.10 • Clang 20", "debug"),
                          ("Ubuntu 24.04 • GCC", "release"),
                          ("Fedora 43 • LLVM 22", "ASan+UBSan")})
        self.assertEqual(selected["windows"]["include"][0]["llvm-version"], "22.1.0")
        self.assertEqual(selected["macos"]["include"][0]["compiler"], "llvm@23")
        for host in selected.values():
            for job in host["include"]:
                self.assertEqual(job["enable_package_tests"], "OFF")

    def test_full_keeps_package_and_platform_coverage(self):
        selected = select_matrix("full")
        self.assertEqual({host: len(matrix["include"]) for host, matrix in selected.items()},
                         {"linux": 31, "windows": 5, "macos": 8})
        self.assertTrue(any(job.get("enable_package_tests") == "ON" for job in selected["windows"]["include"]))
        self.assertTrue(any(job["compiler"] == "appleclang" for job in selected["macos"]["include"]))
        linux = selected["linux"]["include"]
        self.assertFalse(any(job.get("clang_version") == "20" and job.get("preset") == "alusan-system" for job in linux))
        self.assertTrue(any(job.get("clang_version") == "20" and job.get("preset") == "tsan-system" for job in linux))

    def test_unknown_or_empty_profile_fails(self):
        for profile in ("", "nightly", "FULL"):
            with self.subTest(profile=profile), self.assertRaises(ValueError):
                select_matrix(profile)
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "matrix.json"
            source.write_text(json.dumps({"linux": [], "windows": [], "macos": []}))
            with self.assertRaises(ValueError):
                select_matrix("pr", source)

    def test_cli_outputs_json_for_github_without_full_fallback(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "outputs"
            result = subprocess.run([sys.executable, str(ROOT / "scripts/ci_matrix.py"), "--profile", "pr",
                                     "--github-output", str(output)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            parsed = {key: json.loads(value) for key, value in (line.split("=", 1) for line in output.read_text().splitlines())}
            self.assertEqual(len(parsed["windows"]["include"]), 1)
            bad = subprocess.run([sys.executable, str(ROOT / "scripts/ci_matrix.py"), "--profile", "invalid"],
                                 capture_output=True, text=True)
            self.assertNotEqual(bad.returncode, 0)
            self.assertEqual(bad.stdout, "")


if __name__ == "__main__":
    unittest.main()
