#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/.." && pwd)"
build_dir="${1:-build/debug-system}"
mode="${2:-check}"

if [[ "${build_dir}" != /* ]]; then
    build_dir="${repo_root}/${build_dir}"
fi

if [ ! -f "${build_dir}/compile_commands.json" ]; then
    echo "clang-tidy check requires ${build_dir}/compile_commands.json" >&2
    exit 1
fi

clang_tidy_bin="${CLANG_TIDY_BIN:-clang-tidy}"
default_jobs() {
    local jobs=""
    if command -v getconf >/dev/null 2>&1; then
        jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)"
    fi
    if ! [[ "${jobs}" =~ ^[1-9][0-9]*$ ]] && command -v sysctl >/dev/null 2>&1; then
        jobs="$(sysctl -n hw.logicalcpu 2>/dev/null || true)"
    fi
    if ! [[ "${jobs}" =~ ^[1-9][0-9]*$ ]]; then
        jobs="$(python3 - <<'PY'
import os

print(os.cpu_count() or 1)
PY
)"
    fi
    if ! [[ "${jobs}" =~ ^[1-9][0-9]*$ ]]; then
        jobs=1
    fi
    printf '%s\n' "${jobs}"
}

jobs="${CLANG_TIDY_JOBS:-$(default_jobs)}"
tidy_compdb_dir="$(mktemp -d)"
trap 'rm -rf "${tidy_compdb_dir}"' EXIT

tidy_manifest="$(
    python3 - "${repo_root}" "${build_dir}" "${tidy_compdb_dir}" <<'PY'
import fnmatch
import json
import os
import re
import subprocess
import sys

repo_root = os.path.realpath(sys.argv[1])
build_dir = os.path.realpath(sys.argv[2])
tidy_compdb_dir = os.path.realpath(sys.argv[3])

layout_sensitive_patterns = (
    "tests/smoke/namespace_suite_comment.cpp",
    "tests/cmake/module_manual_include_whitespace/cases.cppm",
    "tests/cmake/module_manual_partial_includes/*.cppm",
    "tests/cmake/module_partial_manual_codegen_includes/*.cppm",
    "tests/cmake/module_header_unit_import_preamble/cases.cppm",
    "tests/cmake/module_partition_import_shorthand/*.cppm",
    "tests/cmake/module_mock_imported_sibling/consumer.cppm",
    "tests/cmake/module_mock_imported_sibling/provider.ixx",
    "tests/cmake/module_name_literal_false_match/*.cppm",
    "tests/cmake/module_name_literal_false_match/*.ixx",
)
generated_mock_surface_paths = {
    "tests/concurrency/cases.cpp",
    "tests/failing/cases.cpp",
    "tests/libassert/cases.cpp",
    "tests/mocking/cases.cpp",
}
clang_name = re.compile(r'(^|[\\/"\s])clang(?:\+\+|-cl)?(?:[-.][^\\/"\s]+)?(?:\.exe)?(?=$|[\\/"\s])', re.IGNORECASE)
shim_include = re.compile(r'^#include\s+"([^"]+)"\s*$')

tracked = subprocess.check_output(
    [
        "git",
        "-c",
        f"safe.directory={repo_root}",
        "-C",
        repo_root,
        "ls-files",
        "--",
        "include",
        "src",
        "tests",
        "tools",
    ],
    text=True,
).splitlines()

allowed_exts = (".c", ".cc", ".cpp", ".cxx", ".cppm", ".ixx", ".c++")
tracked = {
    path
    for path in tracked
    if path.endswith(allowed_exts)
}

def path_is_within(path, root):
    try:
        return os.path.commonpath([path, root]) == root
    except ValueError:
        return False

def should_skip_path(rel_path):
    if rel_path in generated_mock_surface_paths:
        return True
    return any(fnmatch.fnmatch(rel_path, pattern) for pattern in layout_sensitive_patterns)

def compiler_is_clang_like(entry):
    compiler_text = ""
    if "arguments" in entry and entry["arguments"]:
        compiler_text = entry["arguments"][0]
        compiler_name = os.path.basename(compiler_text).lower()
        if compiler_name in {"ccache", "sccache", "distcc", "icecc", "buildcache"} and len(entry["arguments"]) > 1:
            compiler_text = entry["arguments"][1]
    else:
        compiler_text = entry.get("command", "")
    normalized = compiler_text.replace("\\", "/")
    return clang_name.search(normalized) is not None

def clone_entry_for_file(entry, old_abs_path, new_abs_path):
    remapped = dict(entry)
    remapped["file"] = new_abs_path
    if "arguments" in entry:
        remapped["arguments"] = [new_abs_path if arg == old_abs_path else arg for arg in entry["arguments"]]
    if "command" in entry:
        command = entry["command"]
        if old_abs_path in command:
            command = command.replace(old_abs_path, new_abs_path)
        else:
            normalized_old = old_abs_path.replace("\\", "/")
            normalized_new = new_abs_path.replace("\\", "/")
            if normalized_old in command:
                command = command.replace(normalized_old, normalized_new)
        remapped["command"] = command
    return remapped

def remap_generated_test_shim(entry):
    abs_path = os.path.realpath(entry["file"])
    if not abs_path.endswith(".gentest.cpp"):
        return None
    if not path_is_within(abs_path, os.path.join(build_dir, "tests")):
        return None

    try:
        with open(abs_path, encoding="utf-8") as f:
            for _ in range(32):
                line = f.readline()
                if not line:
                    break
                match = shim_include.match(line.strip())
                if not match:
                    continue
                candidate = os.path.realpath(os.path.join(os.path.dirname(abs_path), match.group(1)))
                if not path_is_within(candidate, repo_root):
                    return None
                rel_path = os.path.relpath(candidate, repo_root)
                if rel_path.startswith("tests/") and rel_path in tracked and not should_skip_path(rel_path):
                    return rel_path, clone_entry_for_file(entry, abs_path, candidate)
                return None
    except OSError:
        return None

    return None

def extract_rsp_paths(entry):
    if "arguments" in entry:
        return [token[1:] for token in entry["arguments"] if token.startswith("@")]

    command = entry.get("command", "")
    patterns = (
        r'(?:(?<=^)|(?<=\s))@"([^"]+)"',
        r"(?:(?<=^)|(?<=\s))@'([^']+)'",
        r'(?:(?<=^)|(?<=\s))"@([^"]+)"',
        r"(?:(?<=^)|(?<=\s))'@([^']+)'",
        r'(?:(?<=^)|(?<=\s))@([^\s"\']+)',
    )
    rsp_paths = []
    seen = set()
    for pattern in patterns:
        for match in re.finditer(pattern, command):
            rsp_path = next(group for group in match.groups() if group is not None)
            if rsp_path not in seen:
                seen.add(rsp_path)
                rsp_paths.append(rsp_path)
    return rsp_paths

with open(os.path.join(build_dir, "compile_commands.json"), encoding="utf-8") as f:
    compile_commands = json.load(f)

selected = []
seen = set()
selected_entries = []
for entry in compile_commands:
    abs_path = os.path.realpath(entry["file"])
    if path_is_within(abs_path, build_dir):
        remapped = remap_generated_test_shim(entry)
        if remapped is None:
            continue
        rel_path, entry = remapped
    else:
        try:
            rel_path = os.path.relpath(abs_path, repo_root)
        except ValueError:
            continue

        if rel_path.startswith(".."):
            continue
        if rel_path not in tracked:
            continue
        if should_skip_path(rel_path):
            continue

    if rel_path.endswith((".cppm", ".ixx")) and not compiler_is_clang_like(entry):
        continue
    if rel_path in seen:
        continue

    seen.add(rel_path)
    selected.append(rel_path)
    selected_entries.append(entry)

missing_module_prereqs = []
for entry in selected_entries:
    rel_path = os.path.relpath(os.path.realpath(entry["file"]), repo_root)
    if not rel_path.endswith((".cppm", ".ixx")):
        continue

    work_dir = os.path.realpath(entry.get("directory", build_dir))
    for rsp_path in extract_rsp_paths(entry):
        if not os.path.isabs(rsp_path):
            rsp_path = os.path.join(work_dir, rsp_path)
        rsp_path = os.path.realpath(rsp_path)
        if not os.path.exists(rsp_path):
            missing_module_prereqs.append((rel_path, rsp_path))

if missing_module_prereqs:
    for rel_path, rsp_path in missing_module_prereqs:
        print(
            f"clang-tidy check requires generated module response files for {rel_path}: missing {rsp_path}",
            file=sys.stderr,
        )
    print(
        f"Build the owning targets first. For the default repo gate run: cmake --build {build_dir} --target gentest",
        file=sys.stderr,
    )
    sys.exit(2)

with open(os.path.join(tidy_compdb_dir, "compile_commands.json"), "w", encoding="utf-8") as f:
    json.dump(selected_entries, f, indent=2)

for path in selected:
    print(path)
PY
)"

tidy_files=()
if [ -n "${tidy_manifest}" ]; then
    mapfile -t tidy_files <<<"${tidy_manifest}"
fi

if [ "${#tidy_files[@]}" -eq 0 ]; then
    echo "No compile-db-backed repo files found for clang-tidy check."
    exit 0
fi

args=(
    -p "${tidy_compdb_dir}"
    --quiet
    --exclude-header-filter='.*/(third_party|tests/generated)/.*'
)

if [ "${mode}" = "fix" ]; then
    args+=(--fix --format-style=file)
else
    args+=(--warnings-as-errors='*')
fi

cd "${repo_root}"
"${clang_tidy_bin}" --version
echo "clang-tidy: checking ${#tidy_files[@]} file(s)"
printf '%s\0' "${tidy_files[@]}" | xargs -0 -n1 -P"${jobs}" "${clang_tidy_bin}" "${args[@]}"
