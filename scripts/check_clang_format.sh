#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/.." && pwd)"
clang_format_bin="${CLANG_FORMAT_BIN:-clang-format}"

list_cpp_files() {
    local tracked_file=""
    while IFS= read -r tracked_file; do
        case "${tracked_file}" in
        *.c | *.cc | *.cpp | *.cxx | *.h | *.hh | *.hpp | *.hxx | *.ipp | *.cppm | *.ixx | *.c++ | *.h++)
            printf '%s\n' "${tracked_file}"
            ;;
        esac
    done < <(git -c safe.directory="${repo_root}" -C "${repo_root}" ls-files -- include src tests tools)
}

mapfile -t cpp_files < <(list_cpp_files)

if [ "${#cpp_files[@]}" -eq 0 ]; then
    echo "No C++ files found for clang-format check."
    exit 0
fi

cd "${repo_root}"
"${clang_format_bin}" --version
"${clang_format_bin}" --dry-run --Werror "${cpp_files[@]}"
