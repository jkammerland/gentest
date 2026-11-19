#!/usr/bin/env python3
"""Package a committed Gentest source tree without a host compiler or LLVM."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import stat
import subprocess
import sys
import tarfile
import zipfile
from collections.abc import Iterator
from pathlib import Path


def run(
    *args: str, cwd: Path | None = None, env: dict[str, str] | None = None
) -> str:
    return subprocess.run(
        args, cwd=cwd, env=env, check=True, capture_output=True, text=True
    ).stdout.strip()


def digest(path: Path, algorithm: str) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, algorithm).hexdigest()


def tool_provenance(executable: str) -> dict[str, str]:
    resolved = shutil.which(executable)
    if resolved is None:
        raise ValueError(f"Required packaging tool is unavailable: {executable}")
    return {
        "version": run(resolved, "--version").splitlines()[0],
        "sha256": digest(Path(resolved).resolve(), "sha256"),
    }


COMPILED_SUFFIX = re.compile(
    r"\.(?:a|lib|so(?:\.[0-9]+)*|dylib|dll|exe|o|obj|pcm|ifc)$", re.IGNORECASE
)
BINARY_MAGIC = (
    b"\x7fELF",
    b"MZ",
    b"!<arch>\n",
    b"\xfe\xed\xfa\xce",
    b"\xce\xfa\xed\xfe",
    b"\xfe\xed\xfa\xcf",
    b"\xcf\xfa\xed\xfe",
    b"\xca\xfe\xba\xbe",
    b"\xbe\xba\xfe\xca",
    b"\xca\xfe\xba\xbf",
    b"\xbf\xba\xfe\xca",
)


def release_tree(repo: Path, commit: str, prefix: str) -> dict[str, tuple[int, str]]:
    listing = subprocess.check_output(["git", "ls-tree", "-rtz", commit], cwd=repo)
    tree = {prefix.rstrip("/"): (stat.S_IFDIR, "")}
    for record in listing.split(b"\0")[:-1]:
        metadata, path = record.split(b"\t", 1)
        mode, kind, oid = metadata.split()
        if kind not in (b"tree", b"blob"):
            raise ValueError(f"Unsupported release tree entry: {os.fsdecode(path)}")
        tree[prefix + os.fsdecode(path)] = (
            int(mode, 8), oid.decode("ascii") if kind == b"blob" else ""
        )

    for relative in (
        "CMakeLists.txt",
        "LICENSE",
        "src/runner_impl.cpp",
        "tools/src/main.cpp",
    ):
        if not stat.S_ISREG(tree.get(prefix + relative, (0, ""))[0]):
            raise ValueError(f"Source archive is missing {relative}")
    return tree


def archive_members(archive: Path) -> Iterator[tuple[str, int, bytes]]:
    if archive.suffix == ".zip":
        with zipfile.ZipFile(archive) as zipped:
            for entry in zipped.infolist():
                # Git uses DOS attributes for directories and ordinary non-executables.
                mode = entry.external_attr >> 16
                if entry.is_dir() and (mode == 0 or stat.S_ISDIR(mode)):
                    yield entry.filename.removesuffix("/"), stat.S_IFDIR, b""
                else:
                    yield entry.filename, mode or 0o100644, zipped.read(entry)
        return

    with tarfile.open(archive) as tar:
        for member in tar:
            if member.isdir():
                yield member.name, stat.S_IFDIR, b""
            elif member.issym():
                yield member.name, stat.S_IFLNK, os.fsencode(member.linkname)
            elif member.isfile():
                yield member.name, stat.S_IFREG | member.mode, tar.extractfile(member).read()
            else:
                raise ValueError(f"Unsupported TAR member: {member.name}")


def verify_archive(archive: Path, tree: dict[str, tuple[int, str]], algorithm: str) -> None:
    remaining = tree.copy()
    for name, mode, data in archive_members(archive):
        expected = remaining.pop(name, None)
        if expected is None:
            raise ValueError(f"{archive.name}: unexpected or duplicate member {name}")

        oid = ""
        if mode != stat.S_IFDIR:
            # Git blob IDs cover the object type and length as well as the raw bytes.
            blob = hashlib.new(algorithm, f"blob {len(data)}\0".encode("ascii"))
            blob.update(data)
            oid = blob.hexdigest()
        if stat.S_ISLNK(mode):
            mode = stat.S_IFLNK  # Git stores no permission bits for symlinks.
        if (mode, oid) != expected:
            raise ValueError(f"{archive.name}: {name} differs from Git tree (mode or contents)")
        if stat.S_ISREG(mode) and (COMPILED_SUFFIX.search(name) or data.startswith(BINARY_MAGIC)):
            raise ValueError(f"Source archive contains a compiled artifact: {name}")

    if remaining:
        raise ValueError(f"{archive.name}: missing Git tree member {next(iter(remaining))}")


def package_sources(
    repo: Path,
    artifact_dir: Path,
    ref: str = "HEAD",
    *,
    signing_key: str = "",
    passphrase_file: str = "",
    require_signing: bool = False,
) -> Path:
    if require_signing and not signing_key:
        raise ValueError("GENTEST_REQUIRE_PACKAGE_SIGNING=ON requires GPG_SIGNING_KEY")
    if artifact_dir.exists() and any(artifact_dir.iterdir()):
        raise ValueError(f"Artifact directory must be empty: {artifact_dir}")
    # Resolve once: dirty files and later ref changes cannot alter the payload.
    commit = run(
        "git",
        "rev-parse",
        "--verify",
        "--end-of-options",
        f"{ref}^{{commit}}",
        cwd=repo,
    )
    cmake_lists = run("git", "show", f"{commit}:CMakeLists.txt", cwd=repo)
    version_match = re.search(
        r"^\s*VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)\b", cmake_lists, re.MULTILINE
    )
    if version_match is None:
        raise ValueError(
            "Could not read the Gentest project version from the release commit"
        )
    version = version_match[1]
    package_id = f"gentest-{version}-source"
    prefix = f"{package_id}/"
    artifact_dir = artifact_dir.resolve()
    artifact_dir.mkdir(parents=True, exist_ok=True)
    archives: dict[str, dict[str, str]] = {}
    tree = release_tree(repo, commit, prefix)
    object_format = run("git", "rev-parse", "--show-object-format", cwd=repo)
    for archive_format, suffix in (("tar.gz", ".tar.gz"), ("zip", ".zip")):
        archive = artifact_dir / f"{package_id}{suffix}"
        run(
            "git",
            "-c",
            "tar.umask=0022",
            "-c",
            "core.autocrlf=false",
            "-c",
            "core.eol=lf",
            "archive",
            f"--format={archive_format}",
            f"--prefix={prefix}",
            f"--output={archive}",
            commit,
            cwd=repo,
            env={**os.environ, "TZ": "UTC0"},
        )
        verify_archive(archive, tree, object_format)
        archives[archive.name] = {
            algorithm: digest(archive, algorithm) for algorithm in ("sha256", "sha512")
        }

    tools = {"git": tool_provenance("git"), "python": tool_provenance(sys.executable)}
    if signing_key:
        tools["gpg"] = tool_provenance("gpg")
    manifest = artifact_dir / f"{package_id}.manifest.json"
    manifest.write_text(
        json.dumps(
            {
                "schema": "gentest.release-artifact.v1",
                "name": "gentest",
                "version": version,
                "artifact_kind": "source-archive",
                "portable": True,
                "license": "BSL-1.0",
                "contents": {"runtime": "source", "codegen": "source"},
                "source": {
                    "commit": commit,
                    "tree": run("git", "rev-parse", f"{commit}^{{tree}}", cwd=repo),
                },
                "requirements": {
                    "consumer_toolchain": True,
                    "llvm_clang_to_build_codegen": True,
                },
                "archives": archives,
                "packaging_tools": tools,
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )

    for asset in [*(artifact_dir / name for name in archives), manifest]:
        for algorithm in ("sha256", "sha512"):
            checksum = artifact_dir / f"{asset.name}.{algorithm}"
            checksum.write_text(
                f"{digest(asset, algorithm)}  {asset.name}\n", encoding="utf-8"
            )
        if signing_key:
            gpg_args = [
                "gpg",
                "--batch",
                "--yes",
                "--armor",
                "--detach-sign",
                "--local-user",
                signing_key,
            ]
            if passphrase_file:
                gpg_args += [
                    "--pinentry-mode",
                    "loopback",
                    "--passphrase-file",
                    passphrase_file,
                ]
            signature = artifact_dir / f"{asset.name}.asc"
            run(*gpg_args, "--output", str(signature), str(asset))
            run("gpg", "--batch", "--verify", str(signature), str(asset))
    if signing_key:
        public_key = run("gpg", "--batch", "--armor", "--export", signing_key)
        if not public_key:
            raise ValueError("Failed to export the release public key")
        (artifact_dir / f"gentest-{version}-public-key.asc").write_text(
            public_key + "\n", encoding="utf-8"
        )
    return manifest


def main() -> None:
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "artifact_dir",
        type=Path,
        nargs="?",
        default=repo / "build" / "source-release" / "artifacts",
    )
    parser.add_argument(
        "--ref", default="HEAD", help="commit or tag to archive (defaults to HEAD)"
    )
    args = parser.parse_args()
    try:
        manifest = package_sources(
            repo,
            args.artifact_dir,
            args.ref,
            signing_key=os.environ.get("GPG_SIGNING_KEY", ""),
            passphrase_file=os.environ.get("GPG_PASSPHRASE_FILE", ""),
            require_signing=os.environ.get("GENTEST_REQUIRE_PACKAGE_SIGNING", "OFF")
            == "ON",
        )
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        details = (
            exc.stderr.strip()
            if isinstance(exc, subprocess.CalledProcessError) and exc.stderr
            else str(exc)
        )
        parser.exit(1, f"Source packaging failed: {details}\n")
    print(f"Validated source release artifacts in {manifest.parent}")


if __name__ == "__main__":
    main()
