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
jobs="${CLANG_TIDY_JOBS:-$(nproc)}"

tidy_manifest="$(
    python3 - "${repo_root}" "${build_dir}" <<'PY'
import json
import os
import shlex
import subprocess
import sys

repo_root = os.path.realpath(sys.argv[1])
build_dir = os.path.realpath(sys.argv[2])

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

with open(os.path.join(build_dir, "compile_commands.json"), encoding="utf-8") as f:
    compile_commands = json.load(f)

selected = []
seen = set()
selected_entries = []
for entry in compile_commands:
    abs_path = os.path.realpath(entry["file"])
    try:
        rel_path = os.path.relpath(abs_path, repo_root)
    except ValueError:
        continue

    if rel_path.startswith(".."):
        continue
    if os.path.commonpath([abs_path, build_dir]) == build_dir:
        continue
    if rel_path not in tracked:
        continue
    if rel_path in seen:
        continue

    seen.add(rel_path)
    selected.append(rel_path)
    selected_entries.append((rel_path, entry))

missing_module_prereqs = []
for rel_path, entry in selected_entries:
    if not rel_path.endswith((".cppm", ".ixx")):
        continue

    if "arguments" in entry:
        tokens = entry["arguments"]
    else:
        tokens = shlex.split(entry["command"])

    work_dir = os.path.realpath(entry.get("directory", build_dir))
    for token in tokens:
        if not token.startswith("@"):
            continue

        rsp_path = token[1:]
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
    -p "${build_dir}"
    --quiet
    --exclude-header-filter='.*/third_party/.*'
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
