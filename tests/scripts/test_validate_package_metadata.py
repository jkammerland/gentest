#!/usr/bin/env python3

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VALIDATOR = ROOT / "scripts" / "validate_package_metadata.py"


class PackageMetadataValidatorTests(unittest.TestCase):
    def write_fixture(self, root: Path, *, include_codegen: bool = True) -> None:
        cps_dir = root / "share" / "cps" / "gentest"
        sbom_dir = root / "share" / "sbom" / "gentest"
        release_dir = root / "share" / "gentest"
        cps_dir.mkdir(parents=True)
        sbom_dir.mkdir(parents=True)
        release_dir.mkdir(parents=True)
        (release_dir / "gentest-release-artifact.json").write_text(
            json.dumps(
                {
                    "schema": "gentest.release-artifact.v1",
                    "name": "gentest",
                    "version": "1.0.0",
                    "artifact_kind": "host-developer-kit",
                    "portable": False,
                    "contents": {"runtime": "host-built", "codegen": "host-built"},
                    "host": {"system": "Linux", "processor": "x86_64"},
                    "build_toolchain": {"compiler_id": "Clang", "llvm_major": "20"},
                    "requirements": {
                        "compatible_host_userspace": True,
                        "installed_llvm_runtime": True,
                    },
                }
            ),
            encoding="utf-8",
        )
        (cps_dir / "gentest.cps").write_text(
            json.dumps(
                {
                    "name": "gentest",
                    "version": "1.0.0",
                    "components": {
                        "gentest": {},
                        "gentest_main": {},
                        "gentest_runtime": {},
                    },
                    "default_components": ["gentest"],
                    "license": "BSL-1.0",
                    "requires": {"fmt": {}},
                }
            ),
            encoding="utf-8",
        )
        (sbom_dir / "gentest.spdx.json").write_text(
            json.dumps(
                {
                    "specVersion": "SPDX-3.0.1",
                    "dataLicense": "BSL-1.0",
                    "elements": ["gentest", "gentest_main", "gentest_runtime"],
                }
            ),
            encoding="utf-8",
        )
        (sbom_dir / "gentest-tools.spdx.json").write_text(
            json.dumps(
                {
                    "specVersion": "SPDX-3.0.1",
                    "dataLicense": "BSL-1.0",
                    "elements": ["gentest_codegen" if include_codegen else "other_tool"],
                }
            ),
            encoding="utf-8",
        )

    def run_validator(self, root: Path) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(VALIDATOR), str(root), "--version", "1.0.0"],
            check=False,
            capture_output=True,
            text=True,
        )

    def test_accepts_complete_metadata_nested_below_archive_root(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.write_fixture(root / "archive" / "usr" / "local")
            result = self.run_validator(root)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_rejects_tools_sbom_without_codegen(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.write_fixture(root, include_codegen=False)
            result = self.run_validator(root)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("gentest_codegen", result.stderr)

    def test_rejects_duplicate_cps_documents(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.write_fixture(root / "one")
            duplicate = root / "two" / "share" / "cps" / "gentest" / "gentest.cps"
            duplicate.parent.mkdir(parents=True)
            original = root / "one" / "share" / "cps" / "gentest" / "gentest.cps"
            duplicate.write_text(
                original.read_text(encoding="utf-8"),
                encoding="utf-8",
            )
            result = self.run_validator(root)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("found 2", result.stderr)

    def test_rejects_artifact_that_claims_portability(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.write_fixture(root)
            manifest = root / "share" / "gentest" / "gentest-release-artifact.json"
            artifact = json.loads(manifest.read_text(encoding="utf-8"))
            artifact["portable"] = True
            manifest.write_text(json.dumps(artifact), encoding="utf-8")
            result = self.run_validator(root)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("portable", result.stderr)

    def test_rejects_artifact_that_hides_llvm_runtime_requirement(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.write_fixture(root)
            manifest = root / "share" / "gentest" / "gentest-release-artifact.json"
            artifact = json.loads(manifest.read_text(encoding="utf-8"))
            artifact["requirements"]["installed_llvm_runtime"] = False
            manifest.write_text(json.dumps(artifact), encoding="utf-8")
            result = self.run_validator(root)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("LLVM runtime", result.stderr)


if __name__ == "__main__":
    unittest.main()
