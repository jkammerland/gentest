#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/.." && pwd)"
clang_format_bin="$(python3 "${script_dir}/llvm_tools.py" resolve clang-format "${CLANG_FORMAT_BIN:-}")"

should_skip_lint_path() {
    local rel_path="$1"
    case "${rel_path}" in
    tests/smoke/namespace_suite_comment.cpp | tests/cmake/module_manual_include_whitespace/cases.cppm | tests/cmake/module_manual_partial_includes/*.cppm | tests/cmake/module_partial_manual_codegen_includes/*.cppm | tests/cmake/module_header_unit_import_preamble/cases.cppm | tests/cmake/module_partition_import_shorthand/*.cppm | tests/cmake/module_mock_imported_sibling/consumer.cppm | tests/cmake/module_mock_imported_sibling/provider.ixx | tests/cmake/module_name_literal_false_match/*.cppm | tests/cmake/module_name_literal_false_match/*.ixx)
        return 0
        ;;
    esac

    return 1
}

# Capture Git's status before filtering: process substitution hides failures.
tracked_files="$(git -c safe.directory="${repo_root}" -C "${repo_root}" ls-files -- include src tests tools examples/async experimental/harness)"
cpp_files=()
while IFS= read -r tracked_file; do
    if should_skip_lint_path "${tracked_file}"; then
        continue
    fi
    case "${tracked_file}" in
    *.c | *.cc | *.cpp | *.cxx | *.cu | *.h | *.hh | *.hpp | *.hxx | *.ipp | *.cppm | *.ixx | *.mpp | *.c++ | *.h++)
        cpp_files+=("${tracked_file}")
        ;;
    esac
done <<< "${tracked_files}"

if [ "${#cpp_files[@]}" -eq 0 ]; then
    echo "No tracked C++ files found for clang-format check." >&2
    exit 1
fi

cd "${repo_root}"
"${clang_format_bin}" --version
"${clang_format_bin}" --dry-run --Werror "${cpp_files[@]}"
