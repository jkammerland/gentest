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

tidy_compdb_dir="$(mktemp -d "${build_dir}/clang-tidy.XXXXXX")"
trap 'rm -rf "${tidy_compdb_dir}"' EXIT
tidy_compdb="${tidy_compdb_dir}/compile_commands.json"

tidy_files=()
while IFS= read -r tidy_file; do
    tidy_files+=("${tidy_file}")
done < <(python3 - "${repo_root}" "${build_dir}/compile_commands.json" "${tidy_compdb}" <<'PY'
import json
import pathlib
import re
import shlex
import sys

repo_root = pathlib.Path(sys.argv[1]).resolve()
compile_commands_path = pathlib.Path(sys.argv[2])
output_path = pathlib.Path(sys.argv[3])
compile_commands = json.loads(compile_commands_path.read_text())
build_root = compile_commands_path.parent.resolve()
generated_root = (build_root / "tests" / "generated").resolve()

allowed_prefixes = ("include/", "src/", "tools/", "tests/")
allowed_suffixes = (".c", ".cc", ".cpp", ".cxx", ".cu", ".c++", ".cppm", ".ixx", ".mpp")
include_re = re.compile(r'^\s*#include\s+"([^"]+)"', re.MULTILINE)

def repo_rel(path: pathlib.Path):
    try:
        rel = path.resolve().relative_to(repo_root).as_posix()
    except Exception:
        return None
    if not rel.startswith(allowed_prefixes):
        return None
    if not rel.endswith(allowed_suffixes):
        return None
    if rel.startswith("tests/generated/"):
        return None
    return rel

def mapped_repo_rel(path: pathlib.Path):
    if not str(path).startswith(str(build_root)):
        return None
    if ".gentest." not in path.name:
        return None
    if path.suffix not in (".cpp", ".cppm", ".ixx"):
        return None
    try:
        content = path.read_text()
    except Exception:
        return None
    match = include_re.search(content)
    if not match:
        return None
    return repo_rel((path.parent / match.group(1)).resolve())

def transformed_entry(entry, source_path: pathlib.Path):
    argv = entry.get("arguments")
    if argv is None:
        argv = shlex.split(entry["command"])
    original = str(pathlib.Path(entry["file"]).resolve())
    rewritten = str(source_path)
    replaced = False
    transformed = []
    def resolve_include(include_arg: str):
        include_path = pathlib.Path(include_arg)
        if include_path.is_absolute():
            return include_path.resolve()
        return (pathlib.Path(entry["directory"]) / include_path).resolve()
    index = 0
    while index < len(argv):
        arg = argv[index]
        if arg == original:
            transformed.append(rewritten)
            replaced = True
            index += 1
            continue
        if arg == "-I" and index + 1 < len(argv):
            resolved_include = resolve_include(argv[index + 1])
            if str(resolved_include).startswith(str(generated_root)):
                transformed.extend(["-isystem", argv[index + 1]])
                index += 2
                continue
        if arg.startswith("-I") and len(arg) > 2:
            resolved_include = resolve_include(arg[2:])
            if str(resolved_include).startswith(str(generated_root)):
                transformed.extend(["-isystem", arg[2:]])
                index += 1
                continue
        transformed.append(arg)
        index += 1
    if not replaced:
        transformed.append(rewritten)
    return {
        "directory": entry["directory"],
        "file": rewritten,
        "arguments": transformed,
    }

seen = {}

for entry in compile_commands:
    file_path = pathlib.Path(entry["file"]).resolve()
    rel = repo_rel(file_path)
    priority = 2
    if rel is None:
        rel = mapped_repo_rel(file_path)
        priority = 1
    if rel is None:
        continue
    existing = seen.get(rel)
    if existing is not None and existing[0] >= priority:
        continue
    seen[rel] = (priority, transformed_entry(entry, repo_root / rel))

output_path.write_text(json.dumps([seen[key][1] for key in sorted(seen)], indent = 2))
for rel in sorted(seen):
    print(rel)
PY
)

if [ "${#tidy_files[@]}" -eq 0 ]; then
    echo "No clang-tidy translation units found."
    exit 0
fi

line_filter_json="$(python3 - "${repo_root}" <<'PY'
import json
import pathlib
import subprocess
import sys

repo_root = pathlib.Path(sys.argv[1]).resolve()
suffixes = (".c", ".cc", ".cpp", ".cxx", ".cu", ".c++", ".h", ".hh", ".hpp", ".hxx", ".h++", ".ipp", ".inl", ".cppm", ".ixx", ".mpp")
roots = ("include", "src", "tests", "tools")

def filesystem_repo_files():
    files = []
    for root_name in roots:
        root = repo_root / root_name
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if not path.is_file():
                continue
            rel = path.relative_to(repo_root).as_posix()
            if rel.startswith("tests/generated/"):
                continue
            if rel.endswith(suffixes):
                files.append(rel)
    return sorted(files)

try:
    tracked = subprocess.check_output(
        [
            "git",
            "-c",
            f"safe.directory={repo_root}",
            "-C",
            str(repo_root),
            "ls-files",
            "--",
            *roots,
        ],
        text = True,
    ).splitlines()
except (FileNotFoundError, subprocess.CalledProcessError):
    tracked = filesystem_repo_files()

entries = []
for rel in tracked:
    if not rel.endswith(suffixes):
        continue
    entries.append({"name": str(repo_root / rel), "lines": [[1, 999999]]})
print(json.dumps(entries, separators = (",", ":")))
PY
)"

normal_files=()
module_files=()
for tidy_file in "${tidy_files[@]}"; do
    case "${tidy_file}" in
    *.cppm | *.ixx | *.mpp) module_files+=("${tidy_file}") ;;
    *) normal_files+=("${tidy_file}") ;;
    esac
done

args=(
    -p "${tidy_compdb_dir}"
    --quiet
    "--header-filter=^${repo_root}/(src|include|tests|tools)/"
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
export CLANG_TIDY_BIN="${clang_tidy_bin}"
export MODE="${mode}"
export REPO_ROOT="${repo_root}"
export TIDY_COMPDB_DIR="${tidy_compdb_dir}"
export HEADER_FILTER_REGEX="^${repo_root}/(src|include|tests|tools)/"
export LINE_FILTER_JSON="${line_filter_json}"
run_normal_tidy_file() {
    local source_file="$1"
    if [ "${MODE}" = "fix" ]; then
        exec "${CLANG_TIDY_BIN}" \
            -p "${TIDY_COMPDB_DIR}" \
            --quiet \
            --header-filter="${HEADER_FILTER_REGEX}" \
            --exclude-header-filter='.*/(third_party|tests/generated)/.*' \
            --fix \
            --format-style=file \
            --line-filter="${LINE_FILTER_JSON}" \
            "${source_file}"
    fi
    exec "${CLANG_TIDY_BIN}" \
        -p "${TIDY_COMPDB_DIR}" \
        --quiet \
        --header-filter="${HEADER_FILTER_REGEX}" \
        --exclude-header-filter='.*/(third_party|tests/generated)/.*' \
        --warnings-as-errors=* \
        --line-filter="${LINE_FILTER_JSON}" \
        "${source_file}"
}
if [ "${#normal_files[@]}" -ne 0 ]; then
    normal_jobs="${jobs}"
    if [ "${mode}" = "fix" ]; then
        normal_jobs=1
    fi
    pids=()
    for source_file in "${normal_files[@]}"; do
        run_normal_tidy_file "${source_file}" &
        pids+=("$!")
        if [ "${#pids[@]}" -ge "${normal_jobs}" ]; then
            for pid in "${pids[@]}"; do
                wait "${pid}"
            done
            pids=()
        fi
    done
    for pid in "${pids[@]}"; do
        wait "${pid}"
    done
fi

run_module_tidy() {
    local module_file="$1"
    module_args=()
    while IFS= read -r module_arg; do
        module_args+=("${module_arg}")
    done < <(python3 - "${repo_root}" "${tidy_compdb}" "${module_file}" <<'PY'
import json
import pathlib
import shlex
import sys

repo_root = pathlib.Path(sys.argv[1]).resolve()
compile_commands = json.loads(pathlib.Path(sys.argv[2]).read_text())
target = sys.argv[3]
target_path = (repo_root / target).resolve()

for entry in compile_commands:
    file_path = pathlib.Path(entry["file"]).resolve()
    if file_path != target_path:
        continue
    argv = entry.get("arguments")
    if argv is None:
        argv = shlex.split(entry["command"])
    filtered = []
    skip_next = False
    output_path = None
    for index, arg in enumerate(argv):
        if index == 0:
            continue
        if skip_next:
            skip_next = False
            continue
        if arg in ("-o", "-MF", "-MT", "-MQ"):
            skip_next = True
            continue
        if arg in ("-c", "-MD", "-MMD", "-fmodules-ts"):
            continue
        if arg == str(target_path):
            continue
        if arg.startswith(("-o", "-MF", "-MT", "-MQ", "-fdeps-format=", "-fmodule-mapper=")):
            continue
        filtered.append(arg)
    for arg in filtered:
        print(arg)
    break
else:
    raise SystemExit(f"missing compile command for {target}")
PY
)
    "${clang_tidy_bin}" "${args[@]}" --line-filter="${LINE_FILTER_JSON}" "${module_file}" -- "${module_args[@]}"
}

for module_file in "${module_files[@]}"; do
    run_module_tidy "${module_file}"
done
