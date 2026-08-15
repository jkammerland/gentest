#!/usr/bin/env python3

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "scripts"
sys.dont_write_bytecode = True
sys.path.insert(0, str(SCRIPTS))

import bench_case_layout_scale  # noqa: E402
import bench_case_scale  # noqa: E402
import verify_codegen_parallel  # noqa: E402


class ScaleProjectTests(unittest.TestCase):
    def test_single_tu_gentest_cases_are_header_defined(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            source_dir = Path(tmp)
            bench_case_scale.write_scale_project(source_dir, ROOT, 2, "unused", "unused")

            header = (source_dir / "gentest_cases.hpp").read_text(encoding="utf-8")
            source = (source_dir / "gentest_cases.cpp").read_text(encoding="utf-8")
            cmake = (source_dir / "CMakeLists.txt").read_text(encoding="utf-8")

            self.assertIn("#pragma once", header)
            self.assertIn("inline void case_000000()", header)
            self.assertEqual(source, '#include "gentest_cases.hpp"\n')
            self.assertIn("gentest_cases.cpp gentest_cases.hpp", cmake)

    def test_layout_shards_are_header_defined(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            source_dir = Path(tmp)
            active = bench_case_layout_scale.write_scale_project(
                source_dir,
                ROOT,
                count=2,
                layout="multi-tu",
                shards=2,
                doctest_repository="unused",
                doctest_tag="unused",
            )

            self.assertEqual(active, 2)
            for shard in range(active):
                header_name = f"gentest_cases_{shard:03d}.hpp"
                header = (source_dir / header_name).read_text(encoding="utf-8")
                source = (source_dir / f"gentest_cases_{shard:03d}.cpp").read_text(encoding="utf-8")
                self.assertIn("inline void case_", header)
                self.assertEqual(source, f'#include "{header_name}"\n')


class ParallelOutputTests(unittest.TestCase):
    def test_collects_every_additive_output(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            output_dir = Path(tmp)
            registration_a = output_dir / "tu_0000.header_registration.gentest.cpp"
            registration_b = output_dir / "tu_0001.header_registration.gentest.cpp"
            manifest = output_dir / "target.artifact_manifest.json"
            mock_registry = output_dir / "target_mock_registry__domain.hpp"
            for path in (registration_a, registration_b, manifest, mock_registry):
                path.write_text(path.name, encoding="utf-8")

            command = " ".join(
                (
                    "gentest_codegen",
                    "--tu-out-dir",
                    str(output_dir),
                    "--textual-registration-output",
                    str(registration_a),
                    "--textual-registration-output",
                    str(registration_b),
                    "--artifact-manifest",
                    str(manifest),
                    "--mock-domain-registry-output",
                    str(mock_registry),
                )
            )

            self.assertEqual(
                set(verify_codegen_parallel.collect_outputs(command)),
                {registration_a, registration_b, manifest, mock_registry},
            )


if __name__ == "__main__":
    unittest.main()
