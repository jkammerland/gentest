#!/usr/bin/env python3
"""Check the complete hosted compiler compatibility inventory."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from ci_matrix import MATRIX_FILE, select_matrix
from llvm_tools import LLVM_MAJOR, POLICY


class MatrixTests(unittest.TestCase):
    def test_full_selection_preserves_every_catalog_entry(self):
        catalog = json.loads(MATRIX_FILE.read_text())
        selected = select_matrix()
        self.assertEqual(set(selected), {"linux", "windows", "macos"})
        for host, entries in catalog.items():
            with self.subTest(host=host):
                self.assertTrue(entries)
                self.assertEqual(selected[host]["include"], entries)
                self.assertFalse(any("routine" in entry for entry in entries))
        self.assertTrue(any(job.get("enable_package_tests") == "ON" for job in selected["windows"]["include"]))
        self.assertTrue(any(job["compiler"] == "appleclang" for job in selected["macos"]["include"]))

    def test_primary_allure_and_sanitizer_lanes_are_present(self):
        linux = select_matrix()["linux"]["include"]
        primary = [entry for entry in linux if entry.get("primary") and entry["compiler"] == "clang"]
        self.assertEqual({entry["build_type"] for entry in primary}, {"debug", "ASan+UBSan", "TSan"})
        self.assertTrue(all(entry["clang_version"] == str(LLVM_MAJOR) for entry in primary))
        allure = [entry for entry in linux if entry.get("allure")]
        self.assertEqual(len(allure), 1)
        self.assertEqual(allure[0]["build_type"], "debug")
        self.assertTrue(allure[0]["primary"])
        self.assertEqual(allure[0]["enable_package_tests"], "ON")
        self.assertEqual(next(entry for entry in primary if entry["build_type"] == "ASan+UBSan")["preset"], "alusan-system")
        self.assertEqual(next(entry for entry in primary if entry["build_type"] == "TSan")["preset"], "tsan-system")

    def test_older_compiler_compatibility_and_known_leak_gap_remain_explicit(self):
        selected = select_matrix()
        self.assertTrue({"19", "20", "21", "22"}.issubset(
            {entry.get("clang_version") or entry.get("llvm_version") for entry in selected["linux"]["include"]}))
        self.assertTrue({"21.1.4", "22.1.0", POLICY["release"]}.issubset(
            {entry["llvm-version"] for entry in selected["windows"]["include"]}))
        self.assertTrue({"llvm@20", "llvm@21", f"llvm@{LLVM_MAJOR}"}.issubset(
            {entry["compiler"] for entry in selected["macos"]["include"]}))
        linux = selected["linux"]["include"]
        self.assertFalse(any(entry.get("clang_version") == "20" and entry.get("preset") == "alusan-system" for entry in linux))
        self.assertTrue(any(entry.get("clang_version") == "20" and entry.get("preset") == "tsan-system" for entry in linux))

    def test_windows_effective_configs_are_unique_and_keep_msvc(self):
        windows = select_matrix()["windows"]["include"]
        self.assertEqual(len({(entry["llvm-version"], entry["variant"]) for entry in windows}), len(windows))
        self.assertTrue(all(entry["preset"] == "debug-system" for entry in windows))
        workflow = (ROOT / ".github/workflows/cmake.yml").read_text()
        self.assertIn("if: matrix.preset == 'debug-system'", workflow)
        with tempfile.TemporaryDirectory() as temp:
            catalog = json.loads(MATRIX_FILE.read_text())
            catalog["windows"].append({**catalog["windows"][0], "preset": "release-system"})
            source = Path(temp) / "matrix.json"
            source.write_text(json.dumps(catalog))
            with self.assertRaisesRegex(ValueError, "Duplicate effective Windows"):
                select_matrix(source)

    def test_primary_lanes_reject_an_old_toolchain(self):
        for platform, field, wrong in (("linux", "clang_version", "21"),
                                       ("windows", "llvm-version", "22.1.0"),
                                       ("macos", "compiler", "llvm@21")):
            with self.subTest(platform=platform), tempfile.TemporaryDirectory() as temp:
                catalog = json.loads(MATRIX_FILE.read_text())
                primary = next(entry for entry in catalog[platform] if entry.get("primary"))
                primary[field] = wrong
                source = Path(temp) / "matrix.json"
                source.write_text(json.dumps(catalog))
                with self.assertRaisesRegex(ValueError, f"must use LLVM {LLVM_MAJOR}"):
                    select_matrix(source)

    def test_empty_or_obsolete_catalog_fails(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "matrix.json"
            source.write_text(json.dumps({"linux": [], "windows": [], "macos": []}))
            with self.assertRaisesRegex(ValueError, "Empty"):
                select_matrix(source)
            catalog = json.loads(MATRIX_FILE.read_text())
            catalog["linux"][0]["routine"] = True
            source.write_text(json.dumps(catalog))
            with self.assertRaisesRegex(ValueError, "Routine CI profiles"):
                select_matrix(source)

    def test_cli_outputs_full_inventory_and_rejects_removed_profile_option(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "outputs"
            result = subprocess.run([sys.executable, str(ROOT / "scripts/ci_matrix.py"),
                                     "--github-output", str(output)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            parsed = {key: json.loads(value) for key, value in (line.split("=", 1) for line in output.read_text().splitlines())}
            self.assertEqual(parsed, select_matrix())
            bad = subprocess.run([sys.executable, str(ROOT / "scripts/ci_matrix.py"), "--profile", "pr"],
                                 capture_output=True, text=True)
            self.assertNotEqual(bad.returncode, 0)
            self.assertEqual(bad.stdout, "")


if __name__ == "__main__":
    unittest.main()
