#!/usr/bin/env bash

set -euo pipefail

clang_format_bin="${CLANG_FORMAT_BIN:-clang-format}"

mapfile -t cpp_files < <(
    git ls-files include src tests tools |
        rg '\.(c|cc|cpp|cxx|h|hh|hpp|hxx|ipp|cppm|ixx|c\+\+|h\+\+)$'
)

if [ "${#cpp_files[@]}" -eq 0 ]; then
    echo "No C++ files found for clang-format check."
    exit 0
fi

"${clang_format_bin}" --version
"${clang_format_bin}" --dry-run --Werror "${cpp_files[@]}"
