# Requires:
#  -DBUILD_ROOT=<path>

if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckClangScanDepsProgramResolution.cmake: BUILD_ROOT not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")
include("${SOURCE_DIR}/cmake/GentestPublicModules.cmake")
include("${SOURCE_DIR}/cmake/gentest/CodegenToolchain.cmake")

set(_work_dir "${BUILD_ROOT}/clang_scan_deps_program_resolution")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

function(_gentest_write_fake_scan_deps out_path dir_path)
  file(MAKE_DIRECTORY "${dir_path}")
  if(CMAKE_HOST_WIN32)
    set(_fake_scan_deps "${dir_path}/clang-scan-deps.exe")
    file(WRITE "${_fake_scan_deps}" "")
  else()
    set(_fake_scan_deps "${dir_path}/clang-scan-deps")
    file(WRITE "${_fake_scan_deps}" "#!/bin/sh\nexit 0\n")
    file(CHMOD "${_fake_scan_deps}"
      PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
  endif()
  set(${out_path} "${_fake_scan_deps}" PARENT_SCOPE)
endfunction()

set(_resolver_dir_a "${_work_dir}/a")
set(_resolver_dir_b "${_work_dir}/b")
_gentest_write_fake_scan_deps(_fake_scan_deps_a "${_resolver_dir_a}")
_gentest_write_fake_scan_deps(_fake_scan_deps_b "${_resolver_dir_b}")

set(_old_path "$ENV{PATH}")
set(_old_cmake_program_path "${CMAKE_PROGRAM_PATH}")
# A bare executable request follows PATH even when CMake has a separate
# package/tool prefix; changing PATH must change the selected executable.
set(CMAKE_PROGRAM_PATH "${_resolver_dir_b}")
set(ENV{PATH} "${_resolver_dir_a}")

gentest_resolve_optional_program(_public_bare "clang-scan-deps")
if(NOT _public_bare STREQUAL "${_fake_scan_deps_a}")
  message(FATAL_ERROR
    "Expected gentest_resolve_optional_program() to resolve bare clang-scan-deps via PATH as '${_fake_scan_deps_a}', got '${_public_bare}'")
endif()

set(ENV{PATH} "${_resolver_dir_b}")
gentest_resolve_optional_program(_public_bare_after_path_change "clang-scan-deps")
if(NOT _public_bare_after_path_change STREQUAL "${_fake_scan_deps_b}")
  message(FATAL_ERROR
    "Expected gentest_resolve_optional_program() to refresh bare clang-scan-deps resolution after PATH changed to '${_resolver_dir_b}', got '${_public_bare_after_path_change}'")
endif()

gentest_resolve_optional_program(_public_missing "${_work_dir}/missing/clang-scan-deps")
if(NOT "${_public_missing}" STREQUAL "")
  message(FATAL_ERROR
    "Expected gentest_resolve_optional_program() to reject a missing explicit scanner path, got '${_public_missing}'")
endif()
gentest_resolve_optional_program(_public_directory "${_resolver_dir_a}")
if(NOT "${_public_directory}" STREQUAL "")
  message(FATAL_ERROR
    "Expected gentest_resolve_optional_program() to reject a directory candidate, got '${_public_directory}'")
endif()

set(ENV{PATH} "${_resolver_dir_a}")
gentest_resolve_program_candidate(_fixture_bare "clang-scan-deps")
if(NOT _fixture_bare STREQUAL "${_fake_scan_deps_a}")
  message(FATAL_ERROR
    "Expected gentest_resolve_program_candidate() to resolve bare clang-scan-deps via PATH as '${_fake_scan_deps_a}', got '${_fixture_bare}'")
endif()

set(ENV{PATH} "${_resolver_dir_b}")
gentest_resolve_program_candidate(_fixture_bare_after_path_change "clang-scan-deps")
if(NOT _fixture_bare_after_path_change STREQUAL "${_fake_scan_deps_b}")
  message(FATAL_ERROR
    "Expected gentest_resolve_program_candidate() to refresh bare clang-scan-deps resolution after PATH changed to '${_resolver_dir_b}', got '${_fixture_bare_after_path_change}'")
endif()

gentest_resolve_program_candidate(_fixture_missing "${_work_dir}/missing/clang-scan-deps")
if(NOT "${_fixture_missing}" STREQUAL "")
  message(FATAL_ERROR
    "Expected gentest_resolve_program_candidate() to reject a missing explicit scanner path, got '${_fixture_missing}'")
endif()
gentest_resolve_program_candidate(_fixture_directory "${_resolver_dir_a}")
if(NOT "${_fixture_directory}" STREQUAL "")
  message(FATAL_ERROR
    "Expected gentest_resolve_program_candidate() to reject a directory candidate, got '${_fixture_directory}'")
endif()

set(CXX_COMPILER_CLANG_SCAN_DEPS "${_resolver_dir_a}")
set(ENV{PATH} "")
gentest_find_clang_scan_deps(_scan_deps_from_directory "")
if(NOT "${_scan_deps_from_directory}" STREQUAL "")
  message(FATAL_ERROR
    "Expected gentest_find_clang_scan_deps() to reject directory candidates, got '${_scan_deps_from_directory}'")
endif()
unset(CXX_COMPILER_CLANG_SCAN_DEPS)

set(CXX_COMPILER_CLANG_SCAN_DEPS "clang-scan-deps")
set(ENV{PATH} "${_resolver_dir_a}")
gentest_find_clang_scan_deps(_scan_deps_bare_a "")
if(NOT _scan_deps_bare_a STREQUAL "${_fake_scan_deps_a}")
  message(FATAL_ERROR
    "Expected gentest_find_clang_scan_deps() to resolve bare clang-scan-deps via PATH as '${_fake_scan_deps_a}', got '${_scan_deps_bare_a}'")
endif()

set(ENV{PATH} "${_resolver_dir_b}")
gentest_find_clang_scan_deps(_scan_deps_bare_b "")
if(NOT _scan_deps_bare_b STREQUAL "${_fake_scan_deps_b}")
  message(FATAL_ERROR
    "Expected gentest_find_clang_scan_deps() to refresh bare clang-scan-deps resolution after PATH changed to '${_resolver_dir_b}', got '${_scan_deps_bare_b}'")
endif()
unset(CXX_COMPILER_CLANG_SCAN_DEPS)

# Explicit codegen overrides take precedence over compiler-adjacent discovery.
# Control both sources so the fixture does not depend on the enclosing CI toolchain.
set(_had_codegen_scan_deps_env FALSE)
if(DEFINED ENV{GENTEST_CODEGEN_CLANG_SCAN_DEPS})
  set(_had_codegen_scan_deps_env TRUE)
endif()
set(_old_codegen_scan_deps_env "$ENV{GENTEST_CODEGEN_CLANG_SCAN_DEPS}")

set(GENTEST_CODEGEN_HOST_CLANG "${_resolver_dir_a}/clang++")
set(CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS "${_fake_scan_deps_a}")
unset(GENTEST_CODEGEN_CLANG_SCAN_DEPS)
set(ENV{GENTEST_CODEGEN_CLANG_SCAN_DEPS} "${_fake_scan_deps_b}")
set(CXX_COMPILER_CLANG_SCAN_DEPS "${_fake_scan_deps_a}")
gentest_find_clang_scan_deps(_target_scanner "${GENTEST_CODEGEN_HOST_CLANG}")
if(NOT _target_scanner STREQUAL "${_fake_scan_deps_a}")
  message(FATAL_ERROR "Host-codegen environment changed the target compiler's scanner: '${_target_scanner}'")
endif()
unset(CXX_COMPILER_CLANG_SCAN_DEPS)
_gentest_resolve_codegen_clang_scan_deps(_codegen_env_override)
if(NOT _codegen_env_override STREQUAL "${_fake_scan_deps_b}")
  message(FATAL_ERROR
    "Expected the explicit environment scanner to take precedence over compiler scanner discovery, got '${_codegen_env_override}'")
endif()

set(GENTEST_CODEGEN_CLANG_SCAN_DEPS "${_fake_scan_deps_a}")
_gentest_resolve_codegen_clang_scan_deps(_codegen_cmake_override)
if(NOT _codegen_cmake_override STREQUAL "${_fake_scan_deps_a}")
  message(FATAL_ERROR
    "Expected the explicit CMake scanner to take precedence over the environment override, got '${_codegen_cmake_override}'")
endif()
unset(GENTEST_CODEGEN_CLANG_SCAN_DEPS)

# An invalid explicit selection must remain visible instead of silently choosing another installation.
set(_missing_explicit_scan_deps "${_work_dir}/missing-explicit/clang-scan-deps")
set(ENV{GENTEST_CODEGEN_CLANG_SCAN_DEPS} "${_missing_explicit_scan_deps}")
_gentest_resolve_codegen_clang_scan_deps(_codegen_missing_override)
if(NOT _codegen_missing_override STREQUAL "${_missing_explicit_scan_deps}")
  message(FATAL_ERROR
    "Expected the invalid explicit scanner path to be retained, got '${_codegen_missing_override}'")
endif()

# Automatic discovery requires no explicit override, including the one exported by setup-llvm in CI.
unset(ENV{GENTEST_CODEGEN_CLANG_SCAN_DEPS})
if(NOT CMAKE_HOST_WIN32)
  set(CMAKE_EXECUTABLE_SUFFIX ".exe")
  set(GENTEST_CODEGEN_HOST_CLANG "${_resolver_dir_a}/clang++")
  unset(GENTEST_CODEGEN_CLANG_SCAN_DEPS)
  unset(CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS)
  set(CMAKE_CXX_COMPILER "")
  set(LLVM_TOOLS_BINARY_DIR "")
  set(LLVM_VERSION_MAJOR "")
  set(LLVM_PACKAGE_VERSION "")
  set(ENV{PATH} "")
  _gentest_resolve_codegen_clang_scan_deps(_cross_host_scan_deps)
  if(NOT _cross_host_scan_deps STREQUAL "${_fake_scan_deps_a}")
    message(FATAL_ERROR
      "Expected Linux-host scanner discovery to ignore the Windows target executable suffix and resolve "
      "'${_fake_scan_deps_a}', got '${_cross_host_scan_deps}'")
  endif()
endif()

if(_had_codegen_scan_deps_env)
  set(ENV{GENTEST_CODEGEN_CLANG_SCAN_DEPS} "${_old_codegen_scan_deps_env}")
else()
  unset(ENV{GENTEST_CODEGEN_CLANG_SCAN_DEPS})
endif()
set(ENV{PATH} "${_old_path}")
set(CMAKE_PROGRAM_PATH "${_old_cmake_program_path}")
message(STATUS "clang-scan-deps program resolution regression passed")
