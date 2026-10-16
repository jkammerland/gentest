#!/usr/bin/env python3
"""Check configured native runtime targets own every first-party runtime source once."""
from __future__ import annotations

import argparse
from collections import defaultdict
import json
from pathlib import Path
import subprocess
import sys


EXCLUDED = {"gentest_main.cpp", "gentest_anchor.cpp"}
TARGETS = ("gentest_runtime", "gentest_bench_escape")


def read_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))


def require_list(value, description: str) -> list:
    if not isinstance(value, list):
        raise ValueError(f"Missing or malformed {description}")
    return value


def require_dict(value, description: str) -> dict:
    if not isinstance(value, dict):
        raise ValueError(f"Missing or malformed {description}")
    return value


def first_party_source(path: str, source: Path) -> str | None:
    if not isinstance(path, str) or not path:
        raise ValueError("Source metadata contains an invalid path")
    resolved = (source / path).resolve()
    if resolved.parent == source / "src" and resolved.suffix == ".cpp":
        return resolved.relative_to(source).as_posix()
    return None


def check_ownership(source: Path, owners: list[tuple[str, str]]) -> int:
    expected = {path.relative_to(source).as_posix() for path in (source / "src").glob("*.cpp") if path.name not in EXCLUDED}
    if not expected:
        raise ValueError("No runtime implementation sources found")
    actual = defaultdict(list)
    for owner, path in owners:
        name = first_party_source(path, source)
        if name:
            actual[name].append(owner)
    missing, unexpected = expected - actual.keys(), actual.keys() - expected
    duplicates = {path: targets for path, targets in actual.items() if len(targets) != 1}
    errors = []
    if missing:
        errors.append("Unowned runtime sources: " + ", ".join(sorted(missing)))
    if unexpected:
        errors.append("Unexpected runtime sources: " + ", ".join(sorted(unexpected)))
    if duplicates:
        errors.append("Duplicate runtime ownership: " + "; ".join(f"{path}: {', '.join(targets)}" for path, targets in sorted(duplicates.items())))
    if errors:
        raise ValueError("\n".join(errors))
    return len(expected)


def cmake_sources(build: Path, config: str = "") -> list[tuple[str, str]]:
    reply = build / ".cmake/api/v1/reply"
    indices = sorted(reply.glob("index-*.json"))
    if not indices:
        raise ValueError("Missing CMake File API reply; request codemodel-v2 before configuring")
    index = read_json(indices[-1])
    model = read_json(reply / index["reply"]["codemodel-v2"]["jsonFile"])
    configurations = require_list(model.get("configurations"), "CMake configurations")
    selected = [item for item in configurations if not config or item.get("name") == config]
    if len(selected) != 1:
        raise ValueError("Select exactly one configured CMake configuration with --config")
    references = require_list(selected[0].get("targets"), "CMake targets")
    by_id = {item["id"]: item for item in references}
    roots = [item["id"] for item in references if item["name"] in TARGETS]
    if sum(item["name"] == "gentest_runtime" for item in references) != 1:
        raise ValueError("CMake metadata must contain exactly one gentest_runtime target")
    owners, visited = [], set()

    def visit(target_id: str):
        if target_id in visited:
            return
        visited.add(target_id)
        target = read_json(reply / by_id[target_id]["jsonFile"])
        for item in require_list(target.get("sources"), "CMake target sources"):
            # Headers and extracted object files are not implementation units.
            path = item["path"]
            if Path(path).suffix == ".cpp":
                owners.append((target["name"], path))
        for dependency in target.get("dependencies", []):
            visit(dependency["id"])

    for root in roots:
        visit(root)
    return owners


def meson_sources(metadata) -> list[tuple[str, str]]:
    selected = [target for target in require_list(metadata, "Meson targets") if target["name"] in TARGETS]
    if sorted(target["name"] for target in selected) != sorted(TARGETS):
        raise ValueError("Meson metadata needs exactly one runtime and escape target")
    owners = []
    for target in selected:
        for group in require_list(target.get("target_sources"), "Meson target_sources"):
            if group.get("language") == "cpp":
                owners += [(target["name"], path) for path in require_list(group.get("sources"), "Meson C++ sources")]
    return owners


def bazel_sources(metadata) -> list[tuple[str, str]]:
    metadata = require_dict(metadata, "Bazel aquery")
    targets = {str(target["id"]): target["label"] for target in require_list(metadata.get("targets"), "Bazel configured targets")}
    if "//:gentest_runtime" not in targets.values():
        raise ValueError("Bazel aquery does not contain the configured runtime target")
    owners = []
    seen = set()
    for action in require_list(metadata.get("actions"), "Bazel actions"):
        if action.get("mnemonic") != "CppCompile":
            continue
        target = targets[str(action["targetId"])]
        args = require_list(action.get("arguments"), "Bazel CppCompile arguments")
        # The aquery is already restricted to deps(runtime). Read the compile
        # input, not incidental header arguments or another project's compdb.
        sources = [args[index + 1] for index, arg in enumerate(args[:-1]) if arg in ("-c", "/c")]
        if len(sources) != 1:
            raise ValueError(f"Expected one compile input for {target}")
        key = (str(action["targetId"]), sources[0])
        # PIC/non-PIC actions of the same configured target share ownership.
        if key not in seen:
            owners.append((target, sources[0]))
            seen.add(key)
    return owners


def xmake_sources(metadata) -> list[tuple[str, str]]:
    metadata = require_dict(metadata, "Xmake target")
    if metadata.get("name") != "gentest_runtime":
        raise ValueError("Xmake metadata must describe gentest_runtime")
    batches = require_list(metadata.get("sourcebatches"), "Xmake sourcebatches")
    owners = []
    for batch in batches:
        if batch.get("rulename") == "c++.build" and batch.get("sourcekind") == "cxx":
            owners += [("gentest_runtime", path) for path in require_list(batch.get("sourcefiles"), "Xmake C++ sourcefiles")]
    return owners


XMAKE_QUERY = """
import("core.project.config")
import("core.project.project")
import("core.base.json")
config.load()
local target = assert(project.target("gentest_runtime"))
local batches = {}
for _, batch in pairs(target:sourcebatches()) do
    table.insert(batches, {rulename = batch.rulename, sourcekind = batch.sourcekind, sourcefiles = batch.sourcefiles})
end
print(json.encode({name = target:name(), sourcebatches = batches}))
"""


def query_json(command: list[str], source: Path):
    return json.loads(subprocess.check_output(command, cwd=source, text=True))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    prepare = commands.add_parser("prepare-cmake")
    prepare.add_argument("--build-dir", type=Path, required=True)
    check = commands.add_parser("check")
    check.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[1])
    check.add_argument("--backend", choices=("cmake", "meson", "bazel", "xmake"), required=True)
    check.add_argument("--build-dir", type=Path)
    check.add_argument("--config", default="")
    check.add_argument("--bazel-startup-arg", action="append", default=[])
    check.add_argument("--bazel-arg", action="append", default=[])
    args = parser.parse_args()
    try:
        if args.command == "prepare-cmake":
            query = args.build_dir / ".cmake/api/v1/query/codemodel-v2"
            query.parent.mkdir(parents=True, exist_ok=True)
            query.touch()
            return 0
        source = args.source.resolve()
        if args.backend == "cmake":
            owners = cmake_sources(args.build_dir, args.config)
        elif args.backend == "meson":
            owners = meson_sources(query_json(["meson", "introspect", "--targets", str(args.build_dir)], source))
        elif args.backend == "bazel":
            owners = bazel_sources(query_json(["bazel", *args.bazel_startup_arg, "aquery", *args.bazel_arg,
                'mnemonic("CppCompile", deps(//:gentest_runtime))', "--output=jsonproto", "--include_commandline=true"], source))
        else:
            owners = xmake_sources(query_json(["xmake", "lua", "-c", XMAKE_QUERY], source))
        count = check_ownership(source, owners)
        print(f"{args.backend}: {count} runtime implementation sources owned exactly once")
        return 0
    except (ValueError, KeyError, TypeError, OSError, subprocess.CalledProcessError) as error:
        print(f"Runtime source ownership check failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
