#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/.." && pwd)"
clang_format_bin="${CLANG_FORMAT_BIN:-clang-format}"

should_skip_lint_path() {
    local rel_path="$1"
    case "${rel_path}" in
    tests/cmake/module_same_line_directives/* | tests/cmake/module_multiline_directives/* | tests/cmake/module_manual_include_whitespace/* | tests/cmake/module_name_literal_false_match/* | tests/smoke/namespace_suite_comment.cpp)
        return 0
        ;;
    esac

    return 1
}

list_cpp_files() {
    local tracked_file=""
    while IFS= read -r tracked_file; do
        if should_skip_lint_path "${tracked_file}"; then
            continue
        fi
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
