#!/usr/bin/env bash

set -euo pipefail

build_dir="${1:-build/debug-system}"
mode="${2:-check}"

if [ ! -f "${build_dir}/compile_commands.json" ]; then
    echo "clang-tidy check requires ${build_dir}/compile_commands.json" >&2
    exit 1
fi

clang_tidy_bin="${CLANG_TIDY_BIN:-clang-tidy}"
jobs="${CLANG_TIDY_JOBS:-$(nproc)}"

mapfile -t tidy_files < <(git ls-files 'src/*.cpp' 'tools/src/*.cpp')

if [ "${#tidy_files[@]}" -eq 0 ]; then
    echo "No translation units found for clang-tidy check."
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

"${clang_tidy_bin}" --version
printf '%s\0' "${tidy_files[@]}" | xargs -0 -n1 -P"${jobs}" "${clang_tidy_bin}" "${args[@]}"
