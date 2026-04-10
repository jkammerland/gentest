#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: scripts/poc_cross_riscv64_qemu.sh [--full] [--no-test]

Builds a host `gentest_codegen`, then cross-compiles a subset of repo tests for
Linux/riscv64 and runs them under qemu-riscv64 (via CMake's cross emulator).

Environment overrides:
  HOST_BUILD_DIR   (default: build/host-codegen)
  TARGET_BUILD_DIR (default: build/riscv64-qemu)
  BUILD_TYPE       (default: Debug)
  TARGET_TRIPLE    (default: riscv64-linux-gnu)
  TARGET_CC        (default: <TARGET_TRIPLE>-gcc)
  TARGET_CXX       (default: <TARGET_TRIPLE>-g++)
  QEMU             (default: qemu-riscv64)
  SYSROOT          (default: compiler/runtime-detected; explicit values are forwarded to CMAKE_SYSROOT)
  HOST_CC          (default: unset)
  HOST_CXX         (default: unset)
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
target_build_dir="${TARGET_BUILD_DIR:-$root/build/riscv64-qemu}"
build_type="${BUILD_TYPE:-Debug}"
target_triple="${TARGET_TRIPLE:-riscv64-linux-gnu}"
target_cc="${TARGET_CC:-${target_triple}-gcc}"
target_cxx="${TARGET_CXX:-${target_triple}-g++}"
qemu_prog="${QEMU:-qemu-riscv64}"
loader_name="ld-linux-riscv64-lp64d.so.1"

need_cmd() {
  if [[ "$1" == */* ]]; then
    if [[ -x "$1" ]]; then
      return 0
    fi
  elif command -v "$1" >/dev/null 2>&1; then
    return 0
  fi
  if [[ "$1" == */* ]]; then
    echo "Missing required executable: $1" >&2
  else
    echo "Missing required tool: $1" >&2
  fi
  exit 1
}

has_loader() {
  local root="$1"
  [[ -e "${root}/lib/${loader_name}" \
    || -e "${root}/lib64/${loader_name}" \
    || -e "${root}/lib/lp64d/${loader_name}" \
    || -e "${root}/lib64/lp64d/${loader_name}" \
    || -e "${root}/usr/lib/${loader_name}" \
    || -e "${root}/usr/lib64/${loader_name}" \
    || -e "${root}/usr/lib/lp64d/${loader_name}" \
    || -e "${root}/usr/lib64/lp64d/${loader_name}" ]]
}

need_cmd cmake
need_cmd ninja
need_cmd "${target_cc}"
need_cmd "${target_cxx}"

sysroot="${SYSROOT:-}"
explicit_sysroot=0
if [[ -n "${sysroot}" ]]; then
  explicit_sysroot=1
else
  sysroot="$("${target_cc}" -print-sysroot 2>/dev/null || true)"
fi
if [[ -n "${sysroot}" && -d "${sysroot}" ]]; then
  if ! has_loader "${sysroot}"; then
    loader="$("${target_cc}" -print-file-name="${loader_name}" 2>/dev/null || true)"
    if [[ -n "${loader}" && "${loader}" != "${loader_name}" && -e "${loader}" ]]; then
      sysroot="$(cd "$(dirname "${loader}")/.." && pwd)"
    fi
  fi
fi
if [[ -z "${sysroot}" || ! -d "${sysroot}" ]]; then
  echo "Cross sysroot not found (${target_cc} -print-sysroot returned '${sysroot}')." >&2
  echo "Provide SYSROOT=/path/to/riscv64/sysroot when the compiler does not ship a userspace runtime." >&2
  exit 1
fi
if ! has_loader "${sysroot}"; then
  echo "Cross sysroot '${sysroot}' does not contain ${loader_name}." >&2
  echo "Install a riscv64 glibc/sysroot and/or rerun with SYSROOT=/path/to/sysroot." >&2
  exit 1
fi

if [[ "${run_tests}" -eq 1 ]]; then
  need_cmd "${qemu_prog}"
fi

echo "Repo:            ${root}"
echo "Host build:      ${host_build_dir}"
echo "Target build:    ${target_build_dir}"
echo "Build type:      ${build_type}"
echo "Target triple:   ${target_triple}"
echo "Target CC:       ${target_cc}"
echo "Target CXX:      ${target_cxx}"
echo "QEMU:            ${qemu_prog}"
echo "QEMU -L root:    ${sysroot}"

host_args=()
if [[ -n "${HOST_CC:-}" ]]; then
  host_args+=(-DCMAKE_C_COMPILER="${HOST_CC}")
fi
if [[ -n "${HOST_CXX:-}" ]]; then
  host_args+=(-DCMAKE_CXX_COMPILER="${HOST_CXX}")
fi

cmake -S "${root}" -B "${host_build_dir}" -G Ninja \
  -DCMAKE_BUILD_TYPE="${build_type}" \
  -Dgentest_BUILD_TESTING=OFF \
  -DGENTEST_BUILD_CODEGEN=ON \
  "${host_args[@]}"

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

target_args=(
  -DCMAKE_BUILD_TYPE="${build_type}"
  -DCMAKE_TOOLCHAIN_FILE="${root}/cmake/toolchains/riscv64-linux-gnu.cmake"
  -DCMAKE_C_COMPILER="${target_cc}"
  -DCMAKE_CXX_COMPILER="${target_cxx}"
  -DCMAKE_C_COMPILER_TARGET="${target_triple}"
  -DCMAKE_CXX_COMPILER_TARGET="${target_triple}"
  -DGENTEST_BUILD_CODEGEN=OFF
  -DGENTEST_CODEGEN_EXECUTABLE="${codegen}"
  -DGENTEST_ENABLE_PACKAGE_TESTS=OFF
)

if [[ "${explicit_sysroot}" -eq 1 ]]; then
  target_args+=(-DCMAKE_SYSROOT="${sysroot}")
fi

if [[ "${run_tests}" -eq 1 ]]; then
  target_args+=("-DCMAKE_CROSSCOMPILING_EMULATOR=${qemu_prog};-L;${sysroot}")
fi

if [[ -n "${GENTEST_CODEGEN_HOST_CLANG:-}" ]]; then
  target_args+=(-DGENTEST_CODEGEN_HOST_CLANG="${GENTEST_CODEGEN_HOST_CLANG}")
fi

cmake -S "${root}" -B "${target_build_dir}" -G Ninja "${target_args[@]}"

if [[ "${full}" -eq 1 ]]; then
  cmake --build "${target_build_dir}"
else
  cmake --build "${target_build_dir}" --target gentest_unit_tests gentest_mocking_tests
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
# - unit suite + mock-heavy suite + script-based checks (run via qemu-riscv64)
ctest --test-dir "${target_build_dir}" --output-on-failure \
  -R '^(gentest_codegen_check_valid|unit|unit_inventory|mocking)$'
