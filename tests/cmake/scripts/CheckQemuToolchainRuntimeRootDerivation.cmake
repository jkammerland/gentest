if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckQemuToolchainRuntimeRootDerivation.cmake: SOURCE_DIR not set")
endif()

if(NOT DEFINED BUILD_ROOT)
  message(FATAL_ERROR "CheckQemuToolchainRuntimeRootDerivation.cmake: BUILD_ROOT not set")
endif()

include("${SOURCE_DIR}/cmake/GentestGnuQemuToolchain.cmake")

set(_work_dir "${BUILD_ROOT}/qemu_toolchain_runtime_root")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

function(_assert_runtime_root case_name root_rel loader_rel loader_name)
  set(_root "${_work_dir}/${root_rel}")
  set(_loader "${_root}/${loader_rel}/${loader_name}")

  get_filename_component(_loader_dir "${_loader}" DIRECTORY)
  file(MAKE_DIRECTORY "${_loader_dir}")
  file(WRITE "${_loader}" "")

  _gentest_qemu_cross_set_runtime_root_from_loader(
    "${loader_name}"
    "${_loader}"
    _resolved_root
    lib
    lib64
    lib/lp64d
    lib64/lp64d
    usr/lib
    usr/lib64
    usr/lib/lp64d
    usr/lib64/lp64d)

  get_filename_component(_expected_root "${_root}" REALPATH)
  if(NOT _resolved_root STREQUAL "${_expected_root}")
    message(FATAL_ERROR
      "${case_name}: expected runtime root '${_expected_root}', got '${_resolved_root}' for loader '${_loader}'")
  endif()
endfunction()

_assert_runtime_root(
  "riscv-lp64d"
  "riscv-root"
  "lib/lp64d"
  "ld-linux-riscv64-lp64d.so.1")

_assert_runtime_root(
  "riscv-usr-lp64d"
  "riscv-usr-root"
  "usr/lib/lp64d"
  "ld-linux-riscv64-lp64d.so.1")

_assert_runtime_root(
  "aarch64-lib"
  "aarch64-root"
  "lib"
  "ld-linux-aarch64.so.1")
