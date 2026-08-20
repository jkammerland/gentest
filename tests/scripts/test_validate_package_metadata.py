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
        cps_dir.mkdir(parents=True)
        sbom_dir.mkdir(parents=True)
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
                    "license": "NOASSERTION",
                    "requires": {"fmt": {}},
                }
            ),
            encoding="utf-8",
        )
        (sbom_dir / "gentest.spdx.json").write_text(
            json.dumps(
                {
                    "specVersion": "SPDX-3.0.1",
                    "dataLicense": "NOASSERTION",
                    "elements": ["gentest", "gentest_main", "gentest_runtime"],
                }
            ),
            encoding="utf-8",
        )
        (sbom_dir / "gentest-tools.spdx.json").write_text(
            json.dumps(
                {
                    "specVersion": "SPDX-3.0.1",
                    "dataLicense": "NOASSERTION",
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
            self.write_fixture(root / "two")
            result = self.run_validator(root)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("found 2", result.stderr)


if __name__ == "__main__":
    unittest.main()
