#!/usr/bin/env python3

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate gentest non-CMake wrapper outputs and invoke gentest_codegen."
    )
    parser.add_argument(
        "--mode",
        choices=["suite", "mocks", "textual-mocks"],
        default="suite",
        help=(
            "Generation mode. `suite` emits test/codegen wrappers. `mocks` emits explicit mock surfaces. "
            "`textual-mocks` is a legacy alias for `--mode mocks --kind textual`."
        ),
    )
    parser.add_argument(
        "--kind",
        choices=["textual", "modules"],
        default="textual",
        help="Explicit source/surface kind. No auto-detection is performed.",
    )
    parser.add_argument(
        "--backend",
        choices=["generic", "meson", "xmake", "bazel"],
        default="generic",
        help="Optional backend label used for clearer diagnostics.",
    )
    parser.add_argument("--codegen", required=True, help="Path to gentest_codegen")
    parser.add_argument("--source-root", required=True, help="Project source root")
    parser.add_argument("--out-dir", required=True, help="Directory for generated shim/header outputs")
    parser.add_argument("--wrapper-output", help="Generated shim or module-wrapper path")
    parser.add_argument("--header-output", help="Generated registration header path")
    parser.add_argument("--source-file", help="Original source file backing the shim")
    parser.add_argument("--defs-file", action="append", default=[], help="Explicit textual mock defs file")
    parser.add_argument("--include-root", action="append", default=[], help="Include roots used to rewrite local quoted includes in staged defs")
    parser.add_argument("--public-header", help="Generated public header for textual mock mode")
    parser.add_argument("--module-name", help="Generated public aggregate module name for module mock mode")
    parser.add_argument("--anchor-output", help="Generated anchor source for textual mock mode")
    parser.add_argument("--target-id", help="Stable identifier used for textual mock generated anchors")
    parser.add_argument("--metadata-output", help="Optional metadata JSON emitted by mocks mode")
    parser.add_argument(
        "--mock-metadata",
        action="append",
        default=[],
        help="Metadata JSON emitted by a prior explicit mock generation step",
    )
    parser.add_argument(
        "--external-module-source",
        action="append",
        default=[],
        help="Explicit module-name=source-path mapping forwarded to gentest_codegen",
    )
    parser.add_argument("--depfile", default="", help="Optional depfile path to pass through to gentest_codegen")
    parser.add_argument("--compdb", default="", help="Optional compilation database directory")
    parser.add_argument("--mock-registry", default="", help="Generated mock registry header path")
    parser.add_argument("--mock-impl", default="", help="Generated mock implementation header path")
    parser.add_argument(
        "--clang-arg",
        action="append",
        default=[],
        help="Extra clang/gentest_codegen argument appended after `--`",
    )
    return parser.parse_args()


def normalize_mode_and_kind(args: argparse.Namespace) -> argparse.Namespace:
    if args.mode == "textual-mocks":
        if args.kind != "textual":
            raise SystemExit("`--mode textual-mocks` only supports `--kind textual`.")
        args.mode = "mocks"
    return args


def unsupported_modules_message(backend: str, mode: str) -> str:
    operation = "attach_codegen" if mode == "suite" else "add_mocks"
    if backend == "meson":
        return (
            "Meson "
            + operation
            + "(kind=modules) is intentionally unsupported for now because named-module "
            "dependency handling is not reliable enough in the current Meson/toolchain path. "
            "Use kind=textual for Meson, or use CMake for named modules."
        )
    return (
        "The shared non-CMake helper does not implement "
        + operation
        + "(kind=modules) yet."
    )


def format_index(index: int) -> str:
    return f"{index:04d}"


def shorten_generated_stem(stem: str) -> str:
    sanitized = re.sub(r"[^A-Za-z0-9_]", "_", stem)
    if not sanitized:
        sanitized = "tu"
    if len(sanitized) <= 24:
        return sanitized
    digest = hashlib.md5(sanitized.encode("utf-8")).hexdigest()[:8]
    return f"{sanitized[:16]}_{digest}"


def module_wrapper_output_path(out_dir: pathlib.Path, source_file: pathlib.Path, index: int) -> pathlib.Path:
    return out_dir / f"tu_{format_index(index)}_{shorten_generated_stem(source_file.stem)}.module.gentest{source_file.suffix}"


def default_header_output_path(out_dir: pathlib.Path, source_file: pathlib.Path, index: int) -> pathlib.Path:
    return out_dir / f"tu_{format_index(index)}_{shorten_generated_stem(source_file.stem)}.gentest.h"


def resolve_input_path(path_arg: str, source_root: pathlib.Path) -> pathlib.Path:
    candidate = pathlib.Path(path_arg)
    if candidate.is_absolute():
        return candidate
    cwd_candidate = (pathlib.Path.cwd() / candidate).absolute()
    if cwd_candidate.exists():
        return cwd_candidate
    return (source_root / candidate).absolute()


def write_suite_shim(wrapper_output: pathlib.Path, source_file_for_include: pathlib.Path, header_output: pathlib.Path) -> None:
    header_name = header_output.name
    try:
        wrapper_include = pathlib.Path(os.path.relpath(source_file_for_include, wrapper_output.parent)).as_posix()
    except ValueError:
        wrapper_include = source_file_for_include.as_posix()
    content = f"""// This file is auto-generated by gentest (buildsystem shim).
// Do not edit manually.

// Include the original translation unit so fixture types and test bodies are
// visible for wrappers.
#include "{wrapper_include}"

// Include generated registrations after the original TU is visible.
// During codegen or dependency scans, this header may not exist yet.
#if !defined(GENTEST_CODEGEN) && __has_include("{header_name}")
#include "{header_name}"
#endif
"""
    wrapper_output.parent.mkdir(parents=True, exist_ok=True)
    wrapper_output.write_text(content, encoding="utf-8")


INCLUDE_PATTERN = re.compile(r'#[ \t]*include[ \t]*"([^"]+)"')


def resolve_stageable_include(
    include_path: str,
    source_file: pathlib.Path,
    source_root: pathlib.Path,
    include_roots: list[pathlib.Path],
) -> pathlib.Path | None:
    source_dir = source_file.parent
    normalized_root = source_root.absolute()
    normalized_include_roots = [root.absolute() for root in include_roots]
    if os.path.isabs(include_path):
        return None
    include_candidate = (source_dir / include_path).absolute()
    if not include_candidate.exists() or include_candidate.is_dir():
        return None
    for include_root in normalized_include_roots:
        try:
            include_candidate.relative_to(include_root)
            return include_candidate
        except ValueError:
            continue
    try:
        include_candidate.relative_to(normalized_root)
        return include_candidate
    except ValueError:
        return None


def stable_stage_dep_path(out_dir: pathlib.Path, source_file: pathlib.Path, source_root: pathlib.Path) -> pathlib.Path:
    try:
        stable_key = source_file.absolute().relative_to(source_root.absolute()).as_posix()
    except ValueError:
        stable_key = source_file.absolute().as_posix()
    digest = hashlib.md5(stable_key.encode("utf-8")).hexdigest()
    return out_dir / "deps" / f"{digest}_{source_file.name}"


def stage_textual_file(
    *,
    source_file: pathlib.Path,
    staged_path: pathlib.Path,
    source_root: pathlib.Path,
    include_roots: list[pathlib.Path],
    staged_files: dict[pathlib.Path, pathlib.Path],
) -> pathlib.Path:
    source_file = source_file.absolute()
    if source_file in staged_files:
        return staged_files[source_file]
    staged_files[source_file] = staged_path
    source_text = source_file.read_text(encoding="utf-8")

    def replace_include(match: re.Match[str]) -> str:
        include_path = match.group(1)
        include_candidate = resolve_stageable_include(include_path, source_file, source_root, include_roots)
        if include_candidate is None:
            return match.group(0)
        staged_dep = staged_files.get(include_candidate)
        if staged_dep is None:
            staged_dep = stable_stage_dep_path(
                staged_path.parent.parent if staged_path.parent.name == "deps" else staged_path.parent,
                include_candidate,
                source_root,
            )
            stage_textual_file(
                source_file=include_candidate,
                staged_path=staged_dep,
                source_root=source_root,
                include_roots=include_roots,
                staged_files=staged_files,
            )
        rewritten = pathlib.Path(os.path.relpath(staged_dep, staged_path.parent)).as_posix()
        return match.group(0).replace(f'"{include_path}"', f'"{rewritten}"')

    rewritten = INCLUDE_PATTERN.sub(replace_include, source_text)
    staged_path.parent.mkdir(parents=True, exist_ok=True)
    staged_path.write_text(rewritten if rewritten.endswith("\n") else rewritten + "\n", encoding="utf-8")
    return staged_path


def materialize_textual_mock_defs(
    out_dir: pathlib.Path,
    source_root: pathlib.Path,
    defs_files: list[pathlib.Path],
    include_roots: list[pathlib.Path],
) -> list[pathlib.Path]:
    out_dir.mkdir(parents=True, exist_ok=True)
    materialized: list[pathlib.Path] = []
    staged_files: dict[pathlib.Path, pathlib.Path] = {}
    for index, defs_file in enumerate(defs_files):
        staged_path = out_dir / f"def_{index:04d}_{defs_file.name}"
        materialized.append(
            stage_textual_file(
                source_file=defs_file,
                staged_path=staged_path,
                source_root=source_root,
                include_roots=include_roots,
                staged_files=staged_files,
            )
        )
    return materialized


def materialize_module_suite_source(
    out_dir: pathlib.Path,
    source_root: pathlib.Path,
    source_file: pathlib.Path,
    include_roots: list[pathlib.Path],
) -> pathlib.Path:
    staged_files: dict[pathlib.Path, pathlib.Path] = {}
    staged_path = out_dir / f"suite_{format_index(0)}{source_file.suffix or '.cppm'}"
    return stage_textual_file(
        source_file=source_file,
        staged_path=staged_path,
        source_root=source_root,
        include_roots=include_roots,
        staged_files=staged_files,
    )


def stage_module_mock_defs_path(out_dir: pathlib.Path, defs_file: pathlib.Path, index: int) -> pathlib.Path:
    return out_dir / "defs" / f"def_{format_index(index)}_{defs_file.name}"


def materialize_module_mock_defs(
    out_dir: pathlib.Path,
    source_root: pathlib.Path,
    defs_files: list[pathlib.Path],
    include_roots: list[pathlib.Path],
) -> tuple[list[pathlib.Path], list[pathlib.Path]]:
    defs_dir = out_dir / "defs"
    defs_dir.mkdir(parents=True, exist_ok=True)
    root_support_dir = out_dir / "deps"
    materialized: list[pathlib.Path] = []
    public_files: list[pathlib.Path] = []
    staged_files: dict[pathlib.Path, pathlib.Path] = {}
    for index, defs_file in enumerate(defs_files):
        staged_path = stage_module_mock_defs_path(out_dir, defs_file, index)
        staged_primary = stage_textual_file(
            source_file=defs_file,
            staged_path=staged_path,
            source_root=source_root,
            include_roots=include_roots,
            staged_files=staged_files,
        )
        materialized.append(staged_primary)
        for staged_file in staged_files.values():
            if staged_file not in public_files:
                public_files.append(staged_file)
        for staged_file in list(staged_files.values()):
            staged_file = staged_file.resolve()
            try:
                staged_file.relative_to(defs_dir / "deps")
            except ValueError:
                continue
            root_support_dir.mkdir(parents=True, exist_ok=True)
            root_support = root_support_dir / staged_file.name
            if not root_support.exists() or staged_file.read_bytes() != root_support.read_bytes():
                root_support.write_bytes(staged_file.read_bytes())
            if root_support not in public_files:
                public_files.append(root_support)
    return materialized, public_files


def write_textual_mock_source(wrapper_output: pathlib.Path, defs_files_for_include: list[pathlib.Path], header_output: pathlib.Path) -> None:
    content = """// This file is auto-generated by gentest (buildsystem explicit mocks).
// Do not edit manually.

#include "gentest/mock.h"
"""
    for defs_file in defs_files_for_include:
        try:
            defs_include = pathlib.Path(os.path.relpath(defs_file, wrapper_output.parent)).as_posix()
        except ValueError:
            defs_include = defs_file.as_posix()
        content += f'#include "{defs_include}"\n'
    content += f"""
#if !defined(GENTEST_CODEGEN) && __has_include("{header_output.name}")
#include "{header_output.name}"
#endif
"""
    wrapper_output.parent.mkdir(parents=True, exist_ok=True)
    wrapper_output.write_text(content, encoding="utf-8")


def write_textual_mock_public_header(
    public_header: pathlib.Path,
    defs_files_for_include: list[pathlib.Path],
    mock_registry: pathlib.Path,
    mock_impl: pathlib.Path,
) -> None:
    content = """// This file is auto-generated by gentest (buildsystem explicit mocks).
// Do not edit manually.

#pragma once

#define GENTEST_NO_AUTO_MOCK_INCLUDE 1
#include "gentest/mock.h"
"""
    for defs_file in defs_files_for_include:
        try:
            defs_include = pathlib.Path(os.path.relpath(defs_file, public_header.parent)).as_posix()
        except ValueError:
            defs_include = defs_file.as_posix()
        content += f'#include "{defs_include}"\n'
    content += """#undef GENTEST_NO_AUTO_MOCK_INCLUDE

"""
    content += f'#include "{mock_registry.name}"\n'
    content += f'#include "{mock_impl.name}"\n'
    public_header.parent.mkdir(parents=True, exist_ok=True)
    public_header.write_text(content, encoding="utf-8")


def anchor_symbol_name(target_id: str) -> str:
    sanitized_target_id = re.sub(r"\W", "_", target_id)
    if not sanitized_target_id or sanitized_target_id[0].isdigit():
        sanitized_target_id = f"_{sanitized_target_id}"
    stable_hash = hashlib.sha256(target_id.encode("utf-8")).hexdigest()[:12]
    return f"{sanitized_target_id}_{stable_hash}_explicit_mock_anchor"


def write_anchor_source(anchor_output: pathlib.Path, target_id: str) -> None:
    anchor_symbol = anchor_symbol_name(target_id)
    content = f"""// This file is auto-generated by gentest (buildsystem explicit mocks anchor).
// Do not edit manually.

namespace gentest::detail {{
int {anchor_symbol} = 0;
}} // namespace gentest::detail
"""
    anchor_output.parent.mkdir(parents=True, exist_ok=True)
    anchor_output.write_text(content, encoding="utf-8")


def write_module_mock_aggregate(
    aggregate_output: pathlib.Path,
    module_name: str,
    imported_module_names: list[str],
) -> None:
    content = f"""// This file is auto-generated by gentest (buildsystem explicit mocks aggregate module).
// Do not edit manually.

module;

export module {module_name};

export import gentest;
export import gentest.mock;
"""
    for imported_module_name in imported_module_names:
        content += f"export import {imported_module_name};\n"
    aggregate_output.parent.mkdir(parents=True, exist_ok=True)
    aggregate_output.write_text(content, encoding="utf-8")


def load_mock_metadata(metadata_path: pathlib.Path) -> dict:
    try:
        return json.loads(metadata_path.read_text(encoding="utf-8"))
    except (FileNotFoundError, json.JSONDecodeError) as exc:
        raise SystemExit(f"failed to load mock metadata '{metadata_path}': {exc}") from exc


def mock_metadata_module_mappings(metadata: dict) -> list[str]:
    mappings: list[str] = []
    for entry in metadata.get("module_sources", []):
        module_name = entry.get("module_name", "")
        path_text = entry.get("path", "")
        if module_name and path_text:
            mappings.append(f"{module_name}={path_text}")
    return mappings


def mock_metadata_include_roots(metadata: dict) -> list[str]:
    return [path_text for path_text in metadata.get("include_dirs", []) if path_text]


def append_include_clang_args(clang_args: list[str], include_dirs: list[str]) -> list[str]:
    normalized_args = list(clang_args)
    existing_compact = {arg for arg in clang_args if arg.startswith("-I") and len(arg) > 2}
    existing_split: set[str] = set()
    for index, arg in enumerate(clang_args[:-1]):
        if arg in ("-I", "-isystem", "/I"):
            existing_split.add(clang_args[index + 1])
    for include_dir in include_dirs:
        compact = f"-I{include_dir}"
        if compact in existing_compact or include_dir in existing_split:
            continue
        normalized_args.append(compact)
        existing_compact.add(compact)
    return normalized_args


def write_mock_metadata(
    metadata_output: pathlib.Path,
    *,
    backend: str,
    kind: str,
    target_id: str,
    out_dir: pathlib.Path,
    include_dirs: list[pathlib.Path],
    public_surface: dict,
    module_sources: list[tuple[str, pathlib.Path]],
    support_headers: list[pathlib.Path],
) -> None:
    payload = {
        "schema_version": 1,
        "mode": "mocks",
        "backend": backend,
        "kind": kind,
        "target_id": target_id,
        "out_dir": str(out_dir),
        "include_dirs": [str(path) for path in include_dirs],
        "public_surface": public_surface,
        "module_sources": [{"module_name": module_name, "path": str(path)} for module_name, path in module_sources],
        "support_headers": [str(path) for path in support_headers],
    }
    metadata_output.parent.mkdir(parents=True, exist_ok=True)
    metadata_output.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def infer_compdb_dir(codegen_path: pathlib.Path) -> pathlib.Path | None:
    candidate = codegen_path.parent
    remaining = 8
    while candidate is not None and remaining > 0:
        if (candidate / "compile_commands.json").exists():
            return candidate
        if candidate.parent == candidate:
            break
        candidate = candidate.parent
        remaining -= 1
    return None


def resolve_compdb_dir(compdb_arg: str, codegen_path: pathlib.Path, *, infer_default: bool = True) -> pathlib.Path | None:
    if compdb_arg:
        return pathlib.Path(compdb_arg).resolve()
    if infer_default:
        return infer_compdb_dir(codegen_path)
    return None


def load_mock_metadata_set(metadata_args: list[str], source_root: pathlib.Path) -> tuple[list[str], list[str]]:
    metadata_include_roots: list[str] = []
    metadata_module_sources: list[str] = []
    for metadata_arg in metadata_args:
        metadata = load_mock_metadata(resolve_input_path(metadata_arg, source_root))
        metadata_include_roots.extend(mock_metadata_include_roots(metadata))
        metadata_module_sources.extend(mock_metadata_module_mappings(metadata))
    return metadata_include_roots, metadata_module_sources


def normalize_external_module_sources(external_module_sources: list[str], source_root: pathlib.Path) -> list[str]:
    normalized: list[str] = []
    for module_source in external_module_sources:
        if "=" not in module_source:
            normalized.append(module_source)
            continue
        module_name, source_path = module_source.split("=", 1)
        normalized.append(f"{module_name}={resolve_input_path(source_path, source_root)}")
    return normalized


def find_clang_driver() -> pathlib.Path | None:
    def is_clang_driver(candidate: str) -> bool:
        name = pathlib.Path(candidate).name.lower()
        if name.endswith(".exe"):
            name = name[:-4]
        def has_numeric_suffix_after(text: str, prefix: str) -> bool:
            if not text.startswith(prefix):
                return False
            suffix = text[len(prefix):]
            return bool(suffix) and suffix.isdigit()

        return (
            name in ("clang++", "clang", "clang-cl")
            or has_numeric_suffix_after(name, "clang++-")
            or has_numeric_suffix_after(name, "clang-")
        )

    candidates: list[str] = []
    for env_name in ("CLANGXX", "CXX"):
        value = os.environ.get(env_name, "").strip()
        if value and is_clang_driver(value):
            candidates.append(value)
    llvm_bin = os.environ.get("LLVM_BIN", "").strip()
    if llvm_bin:
        llvm_bin_path = pathlib.Path(llvm_bin)
        for suffix in ("", "-22", "-21", "-20"):
            if suffix == "":
                candidates.append(str(llvm_bin_path / "clang++"))
            else:
                candidates.append(str(llvm_bin_path / f"clang++{suffix}"))
        candidates.extend(
            [
                str(llvm_bin_path / "clang-cl"),
            ]
        )
    candidates.extend(["clang++-22", "clang++-21", "clang++-20", "clang++", "clang-cl"])
    for candidate in candidates:
        resolved = shutil.which(candidate)
        if resolved:
            return pathlib.Path(resolved).resolve()
        candidate_path = pathlib.Path(candidate)
        if candidate_path.is_absolute() and candidate_path.exists():
            return candidate_path.resolve()
    return None


def query_clang_resource_dir() -> str | None:
    driver = find_clang_driver()
    if driver is None:
        return None
    try:
        completed = subprocess.run(
            [str(driver), "-print-resource-dir"],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError):
        return None
    resource_dir = completed.stdout.strip()
    return resource_dir or None


def derive_clang_c_driver(clang_cxx_driver: pathlib.Path) -> str:
    name = clang_cxx_driver.name
    if "clang++" in name:
        return str(clang_cxx_driver.with_name(name.replace("clang++", "clang", 1)))
    return str(clang_cxx_driver)


def build_codegen_env(*, kind: str, compdb_dir: pathlib.Path | None) -> dict[str, str] | None:
    if kind != "modules" or compdb_dir is not None:
        return None
    clang_driver = find_clang_driver()
    resource_dir = query_clang_resource_dir()
    env = os.environ.copy()
    if clang_driver is not None:
        env["CXX"] = str(clang_driver)
        env["CC"] = derive_clang_c_driver(clang_driver)
    if resource_dir:
        env["GENTEST_CODEGEN_RESOURCE_DIR"] = resource_dir
    return env


def normalize_textual_mock_clang_args(
    clang_args: list[str],
    *,
    source_root: pathlib.Path,
    compdb_dir: pathlib.Path | None,
) -> list[str]:
    if compdb_dir is not None and compdb_dir == source_root:
        return list(clang_args)

    def normalize_path(path_text: str) -> str:
        if not path_text or os.path.isabs(path_text):
            return path_text
        if path_text.startswith("-") or path_text.startswith("@"):
            return path_text
        source_candidate = (source_root / path_text).resolve()
        if source_candidate.exists():
            return str(source_candidate)
        return path_text

    normalized: list[str] = []
    index = 0
    while index < len(clang_args):
        arg = clang_args[index]
        if arg in ("-I", "-isystem", "/I"):
            normalized.append(arg)
            if index + 1 < len(clang_args):
                normalized.append(normalize_path(clang_args[index + 1]))
                index += 2
                continue
            index += 1
            continue
        if arg.startswith("-I") and len(arg) > 2:
            normalized.append("-I" + normalize_path(arg[2:]))
            index += 1
            continue
        if arg.startswith("/I") and len(arg) > 2:
            normalized.append("/I" + normalize_path(arg[2:]))
            index += 1
            continue
        if arg.startswith("-isystem") and len(arg) > len("-isystem"):
            normalized.append("-isystem" + normalize_path(arg[len("-isystem") :]))
            index += 1
            continue
        normalized.append(arg)
        index += 1
    return normalized


def ensure_clang_resource_dir_arg(clang_args: list[str], *, enabled: bool) -> list[str]:
    if not enabled:
        return list(clang_args)
    for arg in clang_args:
        if arg.startswith("-resource-dir="):
            return list(clang_args)
    resource_dir = query_clang_resource_dir()
    if not resource_dir:
        return list(clang_args)
    return ["-resource-dir=" + resource_dir, *clang_args]


def build_codegen_command(
    *,
    codegen_path: pathlib.Path,
    source_root: pathlib.Path,
    out_dir: pathlib.Path,
    header_outputs: list[pathlib.Path],
    scan_inputs: list[pathlib.Path],
    depfile: str,
    compdb_dir: pathlib.Path | None,
    clang_args: list[str],
    mock_registry: str,
    mock_impl: str,
    discover_mocks: bool,
    external_module_sources: list[str],
) -> list[str]:
    command = [
        str(codegen_path),
        "--source-root",
        str(source_root),
        "--tu-out-dir",
        str(out_dir.resolve()),
    ]
    for header_output in header_outputs:
        command.extend(["--tu-header-output", str(header_output)])
    if depfile:
        command.extend(["--depfile", str(pathlib.Path(depfile).resolve())])
    if compdb_dir:
        command.extend(["--compdb", str(compdb_dir)])
    if mock_registry:
        command.extend(["--mock-registry", str(pathlib.Path(mock_registry).resolve())])
    if mock_impl:
        command.extend(["--mock-impl", str(pathlib.Path(mock_impl).resolve())])
    if discover_mocks:
        command.append("--discover-mocks")
    for external_module_source in external_module_sources:
        command.extend(["--external-module-source", external_module_source])
    command.extend(str(scan_input) for scan_input in scan_inputs)
    command.append("--")
    command.extend(clang_args)
    return command


def sanitize_depfile(depfile: str, generated_outputs: list[pathlib.Path]) -> None:
    if not depfile:
        return
    depfile_path = pathlib.Path(depfile).resolve()
    if not depfile_path.exists():
        return
    depfile_text = depfile_path.read_text(encoding="utf-8")
    if ":" not in depfile_text:
        return
    target_text, deps_text = depfile_text.split(":", 1)
    generated_paths = {path.resolve() for path in generated_outputs}
    filtered_deps: list[str] = []
    for dep_entry in deps_text.split():
        dep_path = pathlib.Path(dep_entry)
        if not dep_path.is_absolute():
            dep_path = (depfile_path.parent / dep_path).resolve()
        else:
            dep_path = dep_path.resolve()
        if dep_path in generated_paths:
            continue
        filtered_deps.append(dep_entry)
    depfile_path.write_text(f"{target_text.strip()} : {' '.join(filtered_deps)}\n", encoding="utf-8")


def main() -> int:
    args = normalize_mode_and_kind(parse_args())

    source_root = pathlib.Path(args.source_root).resolve()
    wrapper_output = pathlib.Path(args.wrapper_output).resolve() if args.wrapper_output else None
    out_dir = pathlib.Path(args.out_dir).resolve()
    codegen_path = pathlib.Path(args.codegen).resolve()

    if args.kind == "modules" and args.backend == "meson":
        print(unsupported_modules_message(args.backend, args.mode), file=sys.stderr)
        return 1

    out_dir.mkdir(parents=True, exist_ok=True)

    if args.mode == "suite":
        if not args.source_file:
            print("--source-file is required in suite mode", file=sys.stderr)
            return 1
        if not args.wrapper_output:
            print("--wrapper-output is required in suite mode", file=sys.stderr)
            return 1
        if not args.header_output:
            print("--header-output is required in suite mode", file=sys.stderr)
            return 1
        source_file = resolve_input_path(args.source_file, source_root)
        if not source_file.exists():
            print(f"source file does not exist: {source_file}", file=sys.stderr)
            return 1
        header_output = pathlib.Path(args.header_output).resolve()
        include_roots = [resolve_input_path(root_arg, source_root) for root_arg in args.include_root]
        compdb_dir = resolve_compdb_dir(
            args.compdb,
            codegen_path,
            infer_default=args.kind == "textual" or args.backend == "xmake",
        )
        metadata_include_roots, metadata_module_sources = load_mock_metadata_set(args.mock_metadata, source_root)
        suite_clang_args = normalize_textual_mock_clang_args(
            append_include_clang_args(args.clang_arg, metadata_include_roots),
            source_root=source_root,
            compdb_dir=compdb_dir,
        )
        suite_clang_args = ensure_clang_resource_dir_arg(
            suite_clang_args,
            enabled=args.kind == "modules" and compdb_dir is None,
        )
        if args.kind == "textual":
            write_suite_shim(wrapper_output, source_file, header_output)
            scan_inputs = [wrapper_output]
        else:
            staged_source = materialize_module_suite_source(out_dir, source_root, source_file, include_roots)
            expected_wrapper = module_wrapper_output_path(out_dir, staged_source, 0)
            if wrapper_output != expected_wrapper:
                print(
                    f"module suite wrapper output must be '{expected_wrapper}' for source '{source_file}'",
                    file=sys.stderr,
                )
                return 1
            scan_inputs = [staged_source]
        command = build_codegen_command(
            codegen_path=codegen_path,
            source_root=source_root,
            out_dir=out_dir,
            header_outputs=[header_output],
            scan_inputs=scan_inputs,
            depfile=args.depfile,
            compdb_dir=compdb_dir,
            clang_args=suite_clang_args,
            mock_registry=args.mock_registry,
            mock_impl=args.mock_impl,
            discover_mocks=False,
            external_module_sources=normalize_external_module_sources(args.external_module_source + metadata_module_sources, source_root),
        )
        subprocess.run(command, check=True, env=build_codegen_env(kind=args.kind, compdb_dir=compdb_dir))
        sanitize_depfile(
            args.depfile,
            [wrapper_output, header_output] if args.kind == "textual" else [staged_source, expected_wrapper, header_output],
        )
        if args.kind == "modules":
            expected_wrapper = module_wrapper_output_path(out_dir, staged_source, 0)
            if not expected_wrapper.exists():
                print(f"module wrapper was not generated: {expected_wrapper}", file=sys.stderr)
                return 1
        return 0

    if not args.defs_file:
        print("--defs-file is required in mocks mode", file=sys.stderr)
        return 1
    if not args.anchor_output:
        print("--anchor-output is required in mocks mode", file=sys.stderr)
        return 1
    if not args.target_id:
        print("--target-id is required in mocks mode", file=sys.stderr)
        return 1
    if not args.mock_registry or not args.mock_impl:
        print("--mock-registry and --mock-impl are required in mocks mode", file=sys.stderr)
        return 1
    if args.kind == "textual" and not args.wrapper_output:
        print("--wrapper-output is required for textual mocks mode", file=sys.stderr)
        return 1
    if args.kind == "textual" and not args.header_output:
        print("--header-output is required for textual mocks mode", file=sys.stderr)
        return 1
    if args.kind == "textual" and not args.public_header:
        print("--public-header is required for textual mocks mode", file=sys.stderr)
        return 1
    if args.kind == "modules" and not args.module_name:
        print("--module-name is required for module mocks mode", file=sys.stderr)
        return 1
    if args.kind == "modules" and not args.metadata_output:
        print("--metadata-output is required for module mocks mode", file=sys.stderr)
        return 1

    defs_files: list[pathlib.Path] = []
    for defs_arg in args.defs_file:
        defs_file = resolve_input_path(defs_arg, source_root)
        if not defs_file.exists():
            print(f"defs file does not exist: {defs_file}", file=sys.stderr)
            return 1
        defs_files.append(defs_file)

    anchor_output = pathlib.Path(args.anchor_output).resolve()
    mock_registry = pathlib.Path(args.mock_registry).resolve()
    mock_impl = pathlib.Path(args.mock_impl).resolve()
    include_roots = [resolve_input_path(root_arg, source_root) for root_arg in args.include_root]
    compdb_dir = resolve_compdb_dir(
        args.compdb,
        codegen_path,
        infer_default=args.kind == "textual" or args.backend == "xmake",
    )
    metadata_include_roots, metadata_module_sources = load_mock_metadata_set(args.mock_metadata, source_root)
    normalized_clang_args = normalize_textual_mock_clang_args(
        append_include_clang_args(args.clang_arg, metadata_include_roots),
        source_root=source_root,
        compdb_dir=compdb_dir,
    )
    normalized_clang_args = ensure_clang_resource_dir_arg(
        normalized_clang_args,
        enabled=args.kind == "modules" and compdb_dir is None,
    )
    metadata_output = pathlib.Path(args.metadata_output).resolve() if args.metadata_output else None
    if args.kind == "textual":
        wrapper_output = pathlib.Path(args.wrapper_output).resolve()
        header_output = pathlib.Path(args.header_output).resolve()
        public_header = pathlib.Path(args.public_header).resolve()
        materialized_defs = materialize_textual_mock_defs(out_dir, source_root, defs_files, include_roots)
        write_textual_mock_source(wrapper_output, materialized_defs, header_output)
        write_textual_mock_public_header(public_header, materialized_defs, mock_registry, mock_impl)
        write_anchor_source(anchor_output, args.target_id)

        command = build_codegen_command(
            codegen_path=codegen_path,
            source_root=source_root,
            out_dir=out_dir,
            header_outputs=[header_output],
            scan_inputs=[wrapper_output],
            depfile=args.depfile,
            compdb_dir=compdb_dir,
            clang_args=normalized_clang_args,
            mock_registry=args.mock_registry,
            mock_impl=args.mock_impl,
            discover_mocks=True,
            external_module_sources=normalize_external_module_sources(args.external_module_source + metadata_module_sources, source_root),
        )
        subprocess.run(command, check=True, env=build_codegen_env(kind=args.kind, compdb_dir=compdb_dir))
        sanitize_depfile(args.depfile, [wrapper_output, header_output, public_header, mock_registry, mock_impl, anchor_output, *materialized_defs])
        if metadata_output is not None:
            write_mock_metadata(
                metadata_output,
                backend=args.backend,
                kind="textual",
                target_id=args.target_id,
                out_dir=out_dir,
                include_dirs=[out_dir],
                public_surface={"type": "header", "path": str(public_header)},
                module_sources=[],
                support_headers=[public_header, mock_registry, mock_impl],
            )
        return 0

    materialized_module_defs, public_files = materialize_module_mock_defs(out_dir, source_root, defs_files, include_roots)
    write_anchor_source(anchor_output, args.target_id)
    module_header_outputs = [default_header_output_path(out_dir, defs_file, index) for index, defs_file in enumerate(materialized_module_defs)]
    command = build_codegen_command(
        codegen_path=codegen_path,
        source_root=source_root,
        out_dir=out_dir,
        header_outputs=module_header_outputs,
        scan_inputs=materialized_module_defs,
        depfile=args.depfile,
        compdb_dir=compdb_dir,
        clang_args=normalized_clang_args,
        mock_registry=args.mock_registry,
        mock_impl=args.mock_impl,
        discover_mocks=True,
        external_module_sources=normalize_external_module_sources(args.external_module_source + metadata_module_sources, source_root),
    )
    subprocess.run(command, check=True, env=build_codegen_env(kind=args.kind, compdb_dir=compdb_dir))
    generated_module_wrappers = [module_wrapper_output_path(out_dir, defs_file, index) for index, defs_file in enumerate(materialized_module_defs)]
    sanitize_depfile(
        args.depfile,
        [anchor_output, mock_registry, mock_impl, metadata_output, *materialized_module_defs, *generated_module_wrappers, *module_header_outputs, *public_files],
    )
    missing_module_wrappers = [path for path in generated_module_wrappers if not path.exists()]
    if missing_module_wrappers:
        print(f"module mock defs did not produce named-module wrappers: {missing_module_wrappers}", file=sys.stderr)
        return 1

    module_name_pattern = re.compile(r"(?m)^[ \t]*(?:export[ \t]+)?module[ \t]+([^;]+?)[ \t]*;[ \t]*$")
    module_mappings: list[tuple[str, pathlib.Path]] = []
    imported_module_names: list[str] = []
    for generated_wrapper in generated_module_wrappers:
        wrapper_text = generated_wrapper.read_text(encoding="utf-8")
        match = module_name_pattern.search(wrapper_text)
        if match is None:
            print(f"unable to determine module name from generated wrapper: {generated_wrapper}", file=sys.stderr)
            return 1
        module_name = match.group(1).strip()
        if module_name == "":
            print(f"generated wrapper has empty module name: {generated_wrapper}", file=sys.stderr)
            return 1
        module_mappings.append((module_name, generated_wrapper))
        imported_module_names.append(module_name)

    aggregate_rel = args.module_name.replace(".", "/").replace(":", "/")
    aggregate_output = out_dir / f"{aggregate_rel}.cppm"
    write_module_mock_aggregate(aggregate_output, args.module_name, imported_module_names)
    module_mappings.append((args.module_name, aggregate_output))

    module_support_headers = [path for path in public_files if path.suffix in {".h", ".hh", ".hpp", ".hxx", ".inc"}]
    module_support_headers.extend([mock_registry, mock_impl])
    module_support_headers = list(dict.fromkeys(module_support_headers))
    write_mock_metadata(
        metadata_output,
        backend=args.backend,
        kind="modules",
        target_id=args.target_id,
        out_dir=out_dir,
        include_dirs=[out_dir],
        public_surface={"type": "module", "module_name": args.module_name, "path": str(aggregate_output)},
        module_sources=module_mappings,
        support_headers=module_support_headers,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
