#!/usr/bin/env python3

from __future__ import annotations

import os
import shutil
import subprocess
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


DIAGNOSTIC = "gentest_codegen: invalid_attrs.cpp:4: unknown gentest attribute 'unknown' (will_fail)"


@unittest.skipIf(os.name == "nt" or shutil.which("bash") is None, "the Bazel check scripts are POSIX shell tests")
class NegativeCodegenCheckScriptTests(unittest.TestCase):
    """The negative checks must fail unless codegen rejected the input itself."""

    # (stub exit code, stub stderr, whether the negative check may pass)
    OUTCOMES = (
        ("validation failure", 1, DIAGNOSTIC, True),
        ("crash", 139, "Segmentation fault", False),
        ("missing shared library", 127, "error while loading shared libraries: libclang-cpp.so", False),
        ("unrelated failure with the expected exit code", 1, "fatal error: 'gentest/runner.h' file not found", False),
        ("diagnostic with another exit code", 2, DIAGNOSTIC, False),
        ("unexpected success", 0, "", False),
    )

    def write_stub(self, path: Path, exit_code: int, stderr: str) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(f"#!/bin/sh\ncat >&2 <<'EOF'\n{stderr}\nEOF\nexit {exit_code}\n", encoding="utf-8")
        path.chmod(0o755)

    def test_runfiles_check_requires_the_validation_failure(self) -> None:
        for label, exit_code, stderr, passes in self.OUTCOMES:
            with self.subTest(label), tempfile.TemporaryDirectory() as tmp:
                runfiles = Path(tmp) / "runfiles"
                workspace = runfiles / "gentest"
                self.write_stub(workspace / "gen_tools" / "gentest_codegen", exit_code, stderr)
                (workspace / "tests" / "smoke").mkdir(parents=True)
                (workspace / "tests" / "smoke" / "invalid_attrs.cpp").write_text("", encoding="utf-8")
                (workspace / "include").mkdir()
                env = {**os.environ, "TEST_SRCDIR": str(runfiles), "TEST_WORKSPACE": "gentest", "TEST_TMPDIR": tmp}
                result = subprocess.run(
                    ["bash", str(SCRIPTS / "codegen_check_invalid.sh")], env=env, capture_output=True, text=True, check=False)
                self.assertEqual(result.returncode == 0, passes, result.stderr)

    def test_direct_check_requires_the_validation_failure(self) -> None:
        for label, exit_code, stderr, passes in self.OUTCOMES:
            with self.subTest(label), tempfile.TemporaryDirectory() as tmp:
                stub = Path(tmp) / "gentest_codegen"
                self.write_stub(stub, exit_code, stderr)
                env = {**os.environ, "TEST_TMPDIR": tmp}
                env.pop("TEST_SRCDIR", None)
                result = subprocess.run(
                    ["bash", str(SCRIPTS / "bazel_codegen_check.sh"), str(stub), "invalid"],
                    env=env, capture_output=True, text=True, check=False)
                self.assertEqual(result.returncode == 0, passes, result.stderr)


if __name__ == "__main__":
    unittest.main()
