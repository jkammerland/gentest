#!/usr/bin/env python3
"""Validate the release contract, CPS, and SPDX documents in a Gentest package."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


def load_object(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise SystemExit(f"failed to read JSON object {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise SystemExit(f"expected a JSON object in {path}")
    return value


def find_one(root: Path, relative: str) -> Path:
    matches = sorted(root.glob(f"**/{relative}"))
    if len(matches) != 1:
        raise SystemExit(f"expected one {relative} below {root}, found {len(matches)}")
    return matches[0]


def validate_cps(path: Path, version: str) -> None:
    cps = load_object(path)
    if cps.get("name") != "gentest" or cps.get("version") != version:
        raise SystemExit(f"unexpected Gentest CPS identity in {path}")
    components = cps.get("components")
    if not isinstance(components, dict):
        raise SystemExit(f"missing CPS components in {path}")
    expected = {"gentest", "gentest_main", "gentest_runtime"}
    if not expected.issubset(components):
        raise SystemExit(f"missing CPS components {sorted(expected - set(components))} in {path}")
    if cps.get("default_components") != ["gentest"]:
        raise SystemExit(f"unexpected CPS default components in {path}")
    if cps.get("license") != "BSL-1.0":
        raise SystemExit(f"CPS must record the project license as BSL-1.0 in {path}")
    requires = cps.get("requires", {})
    if not isinstance(requires, dict) or "fmt" not in requires:
        raise SystemExit(f"CPS metadata does not declare its fmt dependency in {path}")


def collect_strings(value: Any) -> list[str]:
    if isinstance(value, str):
        return [value]
    if isinstance(value, list):
        result: list[str] = []
        for item in value:
            result.extend(collect_strings(item))
        return result
    if isinstance(value, dict):
        result = []
        for item in value.values():
            result.extend(collect_strings(item))
        return result
    return []


def validate_sbom(path: Path, expected_names: set[str]) -> None:
    sbom = load_object(path)
    strings = set(collect_strings(sbom))
    if "BSL-1.0" not in strings:
        raise SystemExit(f"SBOM {path} must record the project license as BSL-1.0")
    missing = {name for name in expected_names if not any(name in value for value in strings)}
    if missing:
        raise SystemExit(f"SBOM {path} does not describe {sorted(missing)}")
    if not any("3.0.1" in value for value in strings):
        raise SystemExit(f"SBOM {path} does not identify SPDX 3.0.1")


def validate_release_artifact(path: Path, version: str) -> None:
    artifact = load_object(path)
    expected_fields = {
        "schema": "gentest.release-artifact.v1",
        "name": "gentest",
        "version": version,
        "artifact_kind": "host-developer-kit",
        "portable": False,
    }
    for field, expected in expected_fields.items():
        if artifact.get(field) != expected:
            raise SystemExit(f"release artifact field {field!r} must be {expected!r} in {path}")

    contents = artifact.get("contents")
    if (
        not isinstance(contents, dict)
        or contents.get("runtime") != "host-built"
        or contents.get("codegen") != "host-built"
    ):
        raise SystemExit(f"release artifact must identify its runtime and codegen as host-built in {path}")

    host = artifact.get("host")
    if not isinstance(host, dict) or not host.get("system") or not host.get("processor"):
        raise SystemExit(f"release artifact must identify its host system and processor in {path}")

    toolchain = artifact.get("build_toolchain")
    if (
        not isinstance(toolchain, dict)
        or not toolchain.get("compiler_id")
        or not toolchain.get("llvm_major")
    ):
        raise SystemExit(f"release artifact must identify its compiler and LLVM major version in {path}")

    requirements = artifact.get("requirements")
    if not isinstance(requirements, dict) or requirements.get("installed_llvm_runtime") is not True:
        raise SystemExit(f"host developer kit must disclose its installed LLVM runtime requirement in {path}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path, help="extracted package or install root")
    parser.add_argument("--version", required=True)
    args = parser.parse_args()

    validate_release_artifact(find_one(args.root, "share/gentest/gentest-release-artifact.json"), args.version)
    validate_cps(find_one(args.root, "share/cps/gentest/gentest.cps"), args.version)
    validate_sbom(
        find_one(args.root, "share/sbom/gentest/gentest.spdx.json"),
        {"gentest", "gentest_main", "gentest_runtime"},
    )
    validate_sbom(
        find_one(args.root, "share/sbom/gentest/gentest-tools.spdx.json"),
        {"gentest_codegen"},
    )


if __name__ == "__main__":
    main()
