#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: scripts/poc_cross_aarch64_qemu.sh [--full] [--no-test]

Builds a host `gentest_codegen`, then cross-compiles a subset of repo tests for
Linux/aarch64 and runs them under qemu-aarch64 (via CMake's cross emulator).

Environment overrides:
  HOST_BUILD_DIR   (default: build/host-codegen)
  TARGET_BUILD_DIR (default: build/aarch64-qemu)
  BUILD_TYPE       (default: Debug)
  SYSROOT          (default: compiler-detected)
EOF
}

full=0
run_tests=1
while [[ $# -gt 0 ]]; do
  case "$1" in
    --full) full=1 ;;
    --no-test) run_tests=0 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown arg: $1" >&2; usage; exit 2 ;;
  esac
  shift
done

root=""
if [[ -n "${BUILD_WORKSPACE_DIRECTORY:-}" && -d "${BUILD_WORKSPACE_DIRECTORY}" ]]; then
  root="${BUILD_WORKSPACE_DIRECTORY}"
fi
if [[ -z "${root}" ]]; then
  root="$(git rev-parse --show-toplevel 2>/dev/null || true)"
fi
if [[ -z "${root}" ]]; then
  root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
fi
host_build_dir="${HOST_BUILD_DIR:-$root/build/host-codegen}"
target_build_dir="${TARGET_BUILD_DIR:-$root/build/aarch64-qemu}"
build_type="${BUILD_TYPE:-Debug}"

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "Missing required tool: $1" >&2
    exit 1
  fi
}

need_cmd cmake
need_cmd ninja
need_cmd aarch64-linux-gnu-gcc
need_cmd aarch64-linux-gnu-g++
need_cmd qemu-aarch64

sysroot="${SYSROOT:-}"
if [[ -z "${sysroot}" ]]; then
  sysroot="$(aarch64-linux-gnu-gcc -print-sysroot)"
fi
if [[ -z "${sysroot}" || ! -d "${sysroot}" ]]; then
  echo "Cross sysroot not found (aarch64-linux-gnu-gcc -print-sysroot returned '${sysroot}')." >&2
  exit 1
fi

if [[ ! -e "${sysroot}/lib/ld-linux-aarch64.so.1" && ! -e "${sysroot}/lib64/ld-linux-aarch64.so.1" ]]; then
  loader="$(aarch64-linux-gnu-gcc -print-file-name=ld-linux-aarch64.so.1)"
  if [[ -n "${loader}" && "${loader}" != "ld-linux-aarch64.so.1" && -e "${loader}" ]]; then
    sysroot="$(cd "$(dirname "${loader}")/.." && pwd)"
  fi
  if [[ ! -e "${sysroot}/lib/ld-linux-aarch64.so.1" && ! -e "${sysroot}/lib64/ld-linux-aarch64.so.1" ]]; then
    echo "Cross sysroot '${sysroot}' does not contain ld-linux-aarch64.so.1." >&2
    echo "Install an aarch64 sysroot (glibc) and/or rerun with SYSROOT=/path/to/sysroot." >&2
    exit 1
  fi
fi

echo "Repo:            ${root}"
echo "Host build:      ${host_build_dir}"
echo "Target build:    ${target_build_dir}"
echo "Build type:      ${build_type}"
echo "Aarch64 sysroot: ${sysroot}"

cmake -S "${root}" -B "${host_build_dir}" -G Ninja \
  -DCMAKE_BUILD_TYPE="${build_type}" \
  -Dgentest_BUILD_TESTING=OFF \
  -DGENTEST_BUILD_CODEGEN=ON

cmake --build "${host_build_dir}" --target gentest_codegen

codegen="${host_build_dir}/tools/gentest_codegen"
if [[ ! -x "${codegen}" ]]; then
  codegen="$(find "${host_build_dir}" -maxdepth 4 -type f -name gentest_codegen -perm -111 | head -n 1 || true)"
fi
if [[ -z "${codegen}" || ! -x "${codegen}" ]]; then
  echo "Could not locate a runnable host gentest_codegen under ${host_build_dir}." >&2
  exit 1
fi
echo "Host codegen:    ${codegen}"

toolchain_args=()
if [[ ! -f "${target_build_dir}/CMakeCache.txt" ]]; then
  toolchain_args=(-DCMAKE_TOOLCHAIN_FILE="${root}/cmake/toolchains/aarch64-linux-gnu.cmake")
fi

cmake -S "${root}" -B "${target_build_dir}" -G Ninja \
  -DCMAKE_BUILD_TYPE="${build_type}" \
  "${toolchain_args[@]}" \
  -DCMAKE_SYSROOT="${sysroot}" \
  -DGENTEST_BUILD_CODEGEN=OFF \
  -DGENTEST_CODEGEN_EXECUTABLE="${codegen}" \
  -DGENTEST_ENABLE_PACKAGE_TESTS=OFF

if [[ "${full}" -eq 1 ]]; then
  cmake --build "${target_build_dir}"
else
  cmake --build "${target_build_dir}" --target gentest_unit_tests
fi

if [[ "${run_tests}" -ne 1 ]]; then
  echo "Skipping ctest (--no-test)."
  exit 0
fi

if [[ "${full}" -eq 1 ]]; then
  ctest --test-dir "${target_build_dir}" --output-on-failure
  exit 0
fi

# Quick sanity subset:
# - host tool check (runs natively)
# - unit suite + script-based checks (run via qemu-aarch64)
ctest --test-dir "${target_build_dir}" --output-on-failure \
  -R '^(gentest_codegen_check_valid|unit|unit_counts|unit_list_counts)$'
