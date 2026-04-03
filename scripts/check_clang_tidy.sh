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

mapfile -t tidy_files < <(git -c safe.directory="${repo_root}" -C "${repo_root}" ls-files -- 'src/*.cpp' 'tools/src/*.cpp')

if [ "${#tidy_files[@]}" -eq 0 ]; then
    echo "No clang-tidy translation units found."
    exit 0
fi

args=(
    -p "${build_dir}"
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
