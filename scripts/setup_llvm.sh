#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
major="$(python3 "${script_dir}/llvm_tools.py" policy major)"
release="$(python3 "${script_dir}/llvm_tools.py" policy release)"
root_command=()
if [ "$(id -u)" -ne 0 ]; then
    root_command=(sudo)
fi

# Keep compilers, analysis tools, development libraries, and resource files together.
source /etc/os-release
case "${ID}" in
ubuntu)
    export DEBIAN_FRONTEND=noninteractive
    "${root_command[@]}" apt-get update
    "${root_command[@]}" apt-get install -y --no-install-recommends ca-certificates curl
    curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key |
        "${root_command[@]}" tee /etc/apt/trusted.gpg.d/apt.llvm.org.asc >/dev/null
    printf '%s\n' \
        'Types: deb' \
        "URIs: https://apt.llvm.org/${VERSION_CODENAME}/" \
        "Suites: llvm-toolchain-${VERSION_CODENAME}-${major}" \
        'Components: main' \
        'Signed-By: /etc/apt/trusted.gpg.d/apt.llvm.org.asc' |
        "${root_command[@]}" tee "/etc/apt/sources.list.d/apt-llvm-${major}.sources" >/dev/null
    "${root_command[@]}" apt-get update
    "${root_command[@]}" apt-get install -y --no-install-recommends \
        "clang-${major}" "clang-tools-${major}" "clang-tidy-${major}" "clang-format-${major}" \
        "libclang-${major}-dev" "libclang-cpp${major}-dev" "libclang-rt-${major}-dev" "llvm-${major}-dev"
    prefix="/usr/lib/llvm-${major}"
    ;;
fedora)
    if [ "$(uname -m)" != "x86_64" ]; then
        echo "The pinned Fedora LLVM archive requires an x86_64 host" >&2
        exit 1
    fi
    "${root_command[@]}" dnf -y install curl tar xz gcc-c++ \
        libxml2-devel libedit-devel zlib-ng-compat-devel libzstd-devel ncurses-devel libffi-devel libcurl-devel
    archive_dir="$(mktemp -d)"
    trap 'rm -rf "${archive_dir}"' EXIT
    archive="${archive_dir}/LLVM-${release}-Linux-X64.tar.xz"
    curl --fail --location --show-error --retry 5 --retry-all-errors \
        -o "${archive}" \
        "https://github.com/llvm/llvm-project/releases/download/llvmorg-${release}/LLVM-${release}-Linux-X64.tar.xz"
    expected_sha="$(python3 "${script_dir}/llvm_tools.py" policy linux_archive_sha256)"
    printf '%s  %s\n' "${expected_sha}" "${archive}" | sha256sum --check
    prefix="/opt/llvm-${release}"
    "${root_command[@]}" mkdir -p "${prefix}"
    "${root_command[@]}" tar -xf "${archive}" -C "${prefix}" --strip-components=1
    ;;
*)
    echo "Unsupported LLVM provisioning platform: ${ID}" >&2
    exit 1
    ;;
esac

# Reject incomplete toolchains before exporting their CMake package locations.
for package in llvm/LLVMConfig.cmake clang/ClangConfig.cmake; do
    if [ ! -f "${prefix}/lib/cmake/${package}" ]; then
        echo "Missing LLVM toolchain package: ${prefix}/lib/cmake/${package}" >&2
        exit 1
    fi
done

python3 "${script_dir}/llvm_tools.py" check \
    "${prefix}/bin/clang" "${prefix}/bin/clang++" "${prefix}/bin/clang-tidy" \
    "${prefix}/bin/clang-format" "${prefix}/bin/clang-scan-deps" "${prefix}/bin/llvm-config" "${prefix}/bin/llvm-cov"

if [ -n "${GITHUB_ENV:-}" ]; then
    {
        echo "LLVM_BIN=${prefix}/bin"
        echo "LLVM_DIR=${prefix}/lib/cmake/llvm"
        echo "Clang_DIR=${prefix}/lib/cmake/clang"
        echo "CC=${prefix}/bin/clang"
        echo "CXX=${prefix}/bin/clang++"
        echo "CLANG_TIDY_BIN=${prefix}/bin/clang-tidy"
        echo "CLANG_FORMAT_BIN=${prefix}/bin/clang-format"
        echo "GENTEST_CODEGEN_HOST_CLANG=${prefix}/bin/clang++"
        echo "GENTEST_CODEGEN_CLANG_SCAN_DEPS=${prefix}/bin/clang-scan-deps"
    } >> "${GITHUB_ENV}"
    echo "${prefix}/bin" >> "${GITHUB_PATH}"
fi
