#!/usr/bin/env python3

from __future__ import annotations

import hashlib
import importlib.util
import json
import os
import subprocess
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "package_source_release", ROOT / "scripts" / "package_source_release.py"
)
packaging = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(packaging)


class SourceReleaseTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.repo = self.root / "repo"
        self.repo.mkdir()
        self.artifacts = self.root / "artifacts"
        self.git("init", "--quiet")
        self.git("config", "user.name", "Source release test")
        self.git("config", "user.email", "source-release@example.invalid")
        self.git("config", "commit.gpgsign", "false")
        self.git("config", "core.autocrlf", "false")
        # Packaging must preserve executable bits despite local archive defaults.
        self.git("config", "tar.umask", "0777")
        self.payload = {
            "CMakeLists.txt": b"project(gentest\n    VERSION 1.2.3\n    LANGUAGES CXX)\n",
            "LICENSE": b"Boost Software License - Version 1.0\n",
            "include/gentest/runner.h": b"#pragma once\n",
            "src/runner_impl.cpp": b"int runtime_source;\n",
            "tools/src/main.cpp": b"int main() { return 0; }\n",
            "scripts/build.sh": b"#!/bin/sh\nexit 0\n",
        }
        for name, data in self.payload.items():
            destination = self.repo / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
        self.git("add", ".")
        self.git("update-index", "--chmod=+x", "scripts/build.sh")
        self.commit = self.commit_tree()

    def git(self, *args: str) -> str:
        return subprocess.run(
            ["git", *args], cwd=self.repo, check=True, capture_output=True, text=True
        ).stdout.strip()

    def commit_tree(self) -> str:
        self.git("commit", "--quiet", "-m", "Source fixture")
        return self.git("rev-parse", "HEAD")

    def test_packages_committed_sources_with_checksums_and_provenance(self) -> None:
        (self.repo / "src" / "runner_impl.cpp").write_text(
            "dirty worktree\n", encoding="utf-8"
        )
        (self.repo / "untracked.exe").write_bytes(b"MZuntracked binary")
        manifest_path = packaging.package_sources(self.repo, self.artifacts)
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        self.assertEqual(manifest["artifact_kind"], "source-archive")
        self.assertEqual(
            manifest["contents"], {"runtime": "source", "codegen": "source"}
        )
        self.assertTrue(manifest["portable"])
        self.assertEqual(manifest["version"], "1.2.3")
        self.assertEqual(manifest["source"]["commit"], self.commit)
        self.assertNotIn("host", manifest)
        self.assertNotIn("build_toolchain", manifest)
        self.assertEqual(set(manifest["packaging_tools"]), {"git", "python"})
        self.assertRegex(
            manifest["packaging_tools"]["git"]["version"], r"^git version \S+"
        )
        self.assertRegex(
            manifest["packaging_tools"]["git"]["sha256"], r"^[0-9a-f]{64}$"
        )
        self.assertEqual(
            set(manifest["archives"]),
            {"gentest-1.2.3-source.tar.gz", "gentest-1.2.3-source.zip"},
        )

        prefix = "gentest-1.2.3-source/"
        expected = {prefix + name: data for name, data in self.payload.items()}
        with tarfile.open(self.artifacts / "gentest-1.2.3-source.tar.gz") as archive:
            contents = {
                member.name: archive.extractfile(member).read()
                for member in archive
                if member.isfile()
            }
            self.assertEqual(contents, expected)
            self.assertEqual(archive.getmember(prefix + "scripts/build.sh").mode, 0o755)
            self.assertEqual(
                archive.getmember(prefix + "include/gentest/runner.h").mode, 0o644
            )
        with zipfile.ZipFile(self.artifacts / "gentest-1.2.3-source.zip") as archive:
            contents = {
                member.filename: archive.read(member)
                for member in archive.infolist()
                if not member.is_dir()
            }
            self.assertEqual(contents, expected)
            self.assertEqual(
                (archive.getinfo(prefix + "scripts/build.sh").external_attr >> 16)
                & 0o777,
                0o755,
            )

        for asset in (
            manifest_path,
            self.artifacts / "gentest-1.2.3-source.tar.gz",
            self.artifacts / "gentest-1.2.3-source.zip",
        ):
            for algorithm in ("sha256", "sha512"):
                expected_digest = hashlib.new(algorithm, asset.read_bytes()).hexdigest()
                checksum = (self.artifacts / f"{asset.name}.{algorithm}").read_text(
                    encoding="utf-8"
                )
                self.assertEqual(checksum, f"{expected_digest}  {asset.name}\n")
                if asset.name in manifest["archives"]:
                    self.assertEqual(
                        manifest["archives"][asset.name][algorithm], expected_digest
                    )

    def test_archives_repeat_across_timezone_and_checkout_settings(self) -> None:
        prefix = "gentest-1.2.3-source"
        checksums = {}
        for zone, autocrlf in (("UTC0", "false"), ("EST5", "false"), ("UTC0", "true"), ("EST5", "true")):
            with self.subTest(zone=zone, autocrlf=autocrlf):
                self.git("config", "core.autocrlf", autocrlf)
                output = self.root / f"{zone}-{autocrlf}"
                with patch.dict(os.environ, TZ=zone):
                    packaging.package_sources(self.repo, output, self.commit)

                for suffix in ("tar.gz", "zip"):
                    archive = output / f"{prefix}.{suffix}"
                    checksum = hashlib.sha256(archive.read_bytes()).hexdigest()
                    if zone == "UTC0" and autocrlf == "false":
                        checksums[suffix] = checksum
                    else:
                        self.assertEqual(checksum, checksums[suffix], suffix)

        # Exercise a shebang through the OS after extracting the converted-checkout case.
        output = self.root / "EST5-true"
        with tarfile.open(output / f"{prefix}.tar.gz") as archive:
            archive.extract(f"{prefix}/scripts/build.sh", path=output)
        script = output / prefix / "scripts/build.sh"
        self.assertEqual(script.read_bytes(), self.payload["scripts/build.sh"])
        if os.name == "posix":
            subprocess.run([script], check=True)

    def test_rejects_git_archive_conversions_and_omissions_before_signing(self) -> None:
        # These attributes affect both formats identically.
        attributes = self.repo / ".git/info/attributes"
        for rule, error in (
            ("LICENSE text eol=crlf", "LICENSE differs"),
            ("LICENSE export-ignore", "missing Git tree member .*LICENSE"),
        ):
            with self.subTest(rule=rule):
                attributes.write_text(rule + "\n", encoding="ascii")
                with self.assertRaisesRegex(ValueError, error):
                    packaging.package_sources(self.repo, self.root / rule.split()[1], signing_key="unused")

    def test_rejects_zip_that_loses_executable_bit_before_signing(self) -> None:
        actual_run = packaging.run

        def run(*args: str, **kwargs: object) -> str:
            result = actual_run(*args, **kwargs)
            if "--format=zip" in args:
                archive = next(self.artifacts.glob("*.zip"))
                replacement = archive.with_suffix(".tmp")
                with zipfile.ZipFile(archive) as source, zipfile.ZipFile(replacement, "w") as dest:
                    for member in source.infolist():
                        data = source.read(member)
                        if member.filename.endswith("/scripts/build.sh"):
                            member.external_attr = 0o100644 << 16
                        # zipfile assigns default Unix permissions to a zero DOS attribute.
                        attrs = member.external_attr
                        dest.writestr(member, data)
                        member.external_attr = attrs
                replacement.replace(archive)
            return result

        with (
            patch.object(packaging, "run", side_effect=run),
            self.assertRaisesRegex(ValueError, r"source\.zip: .*scripts/build.sh differs"),
        ):
            packaging.package_sources(self.repo, self.artifacts, signing_key="unused")

    def test_explicit_commit_controls_both_version_and_contents(self) -> None:
        (self.repo / "CMakeLists.txt").write_text(
            "project(gentest\n VERSION 9.0.0\n)\n", encoding="utf-8"
        )
        self.git("add", "CMakeLists.txt")
        self.commit_tree()
        manifest = packaging.package_sources(self.repo, self.artifacts, self.commit)
        data = json.loads(manifest.read_text(encoding="utf-8"))
        self.assertEqual(data["version"], "1.2.3")
        self.assertEqual(data["source"]["commit"], self.commit)

    def test_rejects_tracked_compiled_payload(self) -> None:
        for name, data in (
            ("lib/libgentest.a", b"archive"),
            ("bin/gentest_codegen", b"\x7fELFbinary"),
        ):
            with self.subTest(name=name):
                path = self.repo / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
                self.git("add", name)
                commit = self.commit_tree()
                output = self.root / Path(name).name
                with self.assertRaisesRegex(ValueError, "compiled artifact"):
                    packaging.package_sources(self.repo, output, commit)
                self.git("rm", name)
                self.commit_tree()

    def test_rejects_source_tree_missing_runtime(self) -> None:
        self.git("rm", "src/runner_impl.cpp")
        self.commit_tree()
        with self.assertRaisesRegex(ValueError, "missing src/runner_impl.cpp"):
            packaging.package_sources(self.repo, self.artifacts)

    def test_rejects_nonempty_output_without_replacing_files(self) -> None:
        self.artifacts.mkdir()
        sentinel = self.artifacts / "existing.txt"
        sentinel.write_text("keep\n", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "must be empty"):
            packaging.package_sources(self.repo, self.artifacts)
        self.assertEqual(sentinel.read_text(encoding="utf-8"), "keep\n")

    def test_required_signing_fails_before_creating_unsigned_assets(self) -> None:
        with self.assertRaisesRegex(ValueError, "requires GPG_SIGNING_KEY"):
            packaging.package_sources(self.repo, self.artifacts, require_signing=True)
        self.assertFalse(self.artifacts.exists())

    def test_signature_verification_failure_is_fatal(self) -> None:
        actual_run = packaging.run
        commands = []

        def run(*args: str, **kwargs: object) -> str:
            if args[0] == "gpg":
                commands.append(args)
                if "--verify" in args:
                    raise subprocess.CalledProcessError(1, args, stderr="bad signature")
                return ""
            return actual_run(*args, **kwargs)

        with (
            patch.object(packaging, "run", side_effect=run),
            patch.object(packaging, "tool_provenance", return_value={}),
            self.assertRaises(subprocess.CalledProcessError),
        ):
            packaging.package_sources(
                self.repo, self.artifacts, signing_key="test-key", require_signing=True
            )
        self.assertEqual(len(commands), 2)
        self.assertIn("--detach-sign", commands[0])
        self.assertIn("--verify", commands[1])


if __name__ == "__main__":
    unittest.main()
