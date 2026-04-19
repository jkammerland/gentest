if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckLegacyManifestModeWarnings.cmake: PROG not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckLegacyManifestModeWarnings.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckLegacyManifestModeWarnings.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckLegacyManifestModeWarnings.cmake: GENTEST_SOURCE_DIR not set")
endif()
if(NOT DEFINED CODEGEN_STD OR "${CODEGEN_STD}" STREQUAL "")
  message(FATAL_ERROR "CheckLegacyManifestModeWarnings.cmake: CODEGEN_STD not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")

set(_legacy_warning "--output selects legacy manifest/single-TU mode")
set(_cmake_legacy_warning "OUTPUT selects legacy")

function(_gentest_expect_success_contains label required_substring)
  execute_process(
    COMMAND ${ARGN}
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)

  set(_combined "${_out}\n${_err}")
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR
      "${label}: expected success, got ${_rc}.\n"
      "--- stdout ---\n${_out}\n--- stderr ---\n${_err}")
  endif()

  string(FIND "${_combined}" "${required_substring}" _required_pos)
  if(_required_pos EQUAL -1)
    message(FATAL_ERROR
      "${label}: expected to find '${required_substring}'.\n"
      "--- stdout ---\n${_out}\n--- stderr ---\n${_err}")
  endif()
endfunction()

function(_gentest_expect_success_forbids label forbidden_substring)
  execute_process(
    COMMAND ${ARGN}
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)

  set(_combined "${_out}\n${_err}")
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR
      "${label}: expected success, got ${_rc}.\n"
      "--- stdout ---\n${_out}\n--- stderr ---\n${_err}")
  endif()

  string(FIND "${_combined}" "${forbidden_substring}" _forbidden_pos)
  if(NOT _forbidden_pos EQUAL -1)
    message(FATAL_ERROR
      "${label}: did not expect to find '${forbidden_substring}'.\n"
      "--- stdout ---\n${_out}\n--- stderr ---\n${_err}")
  endif()
endfunction()

set(_compdb_root "${BUILD_ROOT}")
if(DEFINED COMPDB_ROOT AND NOT "${COMPDB_ROOT}" STREQUAL "")
  set(_compdb_root "${COMPDB_ROOT}")
endif()

set(_smoke_source "${SOURCE_DIR}/tests/smoke/codegen_axis_generators.cpp")
if(NOT EXISTS "${_smoke_source}")
  message(FATAL_ERROR "CheckLegacyManifestModeWarnings.cmake: missing smoke source '${_smoke_source}'")
endif()

set(_clang_args)
if(DEFINED TARGET_ARG AND NOT "${TARGET_ARG}" STREQUAL "")
  list(APPEND _clang_args "${TARGET_ARG}")
endif()
gentest_make_public_api_include_args(
  _public_include_args
  SOURCE_ROOT "${SOURCE_DIR}"
  INCLUDE_TESTS
  APPLE_SYSROOT)
list(APPEND _clang_args "${CODEGEN_STD}" ${_public_include_args})

_gentest_expect_success_contains(
  "cli legacy manifest output warning"
  "${_legacy_warning}"
  "${PROG}"
  --output "${BUILD_ROOT}/cli_legacy_manifest.cpp"
  --compdb "${_compdb_root}"
  "${_smoke_source}"
  --
  ${_clang_args})

_gentest_expect_success_forbids(
  "cli per-tu output has no legacy manifest warning"
  "${_legacy_warning}"
  "${PROG}"
  --check
  --tu-out-dir "${BUILD_ROOT}/tu-mode"
  --compdb "${_compdb_root}"
  "${_smoke_source}"
  --
  ${_clang_args})

set(_fixture_source_dir "${BUILD_ROOT}/cmake_legacy_manifest_warning_src")
set(_fixture_build_dir "${BUILD_ROOT}/cmake_legacy_manifest_warning_build")
file(REMOVE_RECURSE "${_fixture_source_dir}" "${_fixture_build_dir}")
file(MAKE_DIRECTORY "${_fixture_source_dir}")
file(WRITE "${_fixture_source_dir}/cases.cpp" [=[
#include "gentest/assertions.h"

[[using gentest: test("legacy_manifest_warning/builds")]]
void legacyManifestWarningBuilds() {
    gentest::asserts::EXPECT_TRUE(true);
}
]=])
file(TO_CMAKE_PATH "${GENTEST_SOURCE_DIR}" _gentest_source_dir_for_cmake)
file(WRITE "${_fixture_source_dir}/CMakeLists.txt.in" [=[
cmake_minimum_required(VERSION 3.31)
project(gentest_legacy_manifest_warning LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
set(gentest_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(GENTEST_BUILD_CODEGEN OFF CACHE BOOL "" FORCE)
set(GENTEST_ENABLE_PUBLIC_MODULES OFF CACHE STRING "" FORCE)

add_subdirectory("@_gentest_source_dir_for_cmake@" gentest)

add_executable(legacy_manifest_warning)
target_link_libraries(legacy_manifest_warning PRIVATE gentest::gentest_main)
target_compile_features(legacy_manifest_warning PRIVATE cxx_std_20)

gentest_attach_codegen(legacy_manifest_warning
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/legacy_manifest_warning.gentest.cpp"
    SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/cases.cpp")
]=])
configure_file("${_fixture_source_dir}/CMakeLists.txt.in" "${_fixture_source_dir}/CMakeLists.txt" @ONLY)

set(_cmake_gen_args -G "${GENERATOR}")
if(DEFINED GENERATOR_PLATFORM AND NOT "${GENERATOR_PLATFORM}" STREQUAL "")
  list(APPEND _cmake_gen_args -A "${GENERATOR_PLATFORM}")
endif()
if(DEFINED GENERATOR_TOOLSET AND NOT "${GENERATOR_TOOLSET}" STREQUAL "")
  list(APPEND _cmake_gen_args -T "${GENERATOR_TOOLSET}")
endif()

set(_cmake_cache_args
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
  "-DGENTEST_CODEGEN_EXECUTABLE=${PROG}")
if(DEFINED MAKE_PROGRAM AND NOT "${MAKE_PROGRAM}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()
if(DEFINED C_COMPILER AND NOT "${C_COMPILER}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_C_COMPILER=${C_COMPILER}")
endif()
if(DEFINED CXX_COMPILER AND NOT "${CXX_COMPILER}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}")
endif()
if(DEFINED CXX_COMPILER_CLANG_SCAN_DEPS AND NOT "${CXX_COMPILER_CLANG_SCAN_DEPS}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=${CXX_COMPILER_CLANG_SCAN_DEPS}")
endif()
if(DEFINED LLVM_DIR AND NOT "${LLVM_DIR}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DLLVM_DIR=${LLVM_DIR}")
endif()
if(DEFINED Clang_DIR AND NOT "${Clang_DIR}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DClang_DIR=${Clang_DIR}")
endif()
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()

_gentest_expect_success_contains(
  "cmake legacy manifest output warning"
  "${_cmake_legacy_warning}"
  "${CMAKE_COMMAND}"
  ${_cmake_gen_args}
  -S "${_fixture_source_dir}"
  -B "${_fixture_build_dir}"
  ${_cmake_cache_args})

set(_build_args --build "${_fixture_build_dir}" --target legacy_manifest_warning)
if(DEFINED BUILD_CONFIG AND NOT "${BUILD_CONFIG}" STREQUAL "")
  list(APPEND _build_args --config "${BUILD_CONFIG}")
elseif(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _build_args --config "${BUILD_TYPE}")
endif()

gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" ${_build_args}
  STRIP_TRAILING_WHITESPACE
  WORKING_DIRECTORY "${BUILD_ROOT}")

set(_per_tu_source_dir "${BUILD_ROOT}/cmake_per_tu_warning_probe_src")
set(_per_tu_build_dir "${BUILD_ROOT}/cmake_per_tu_warning_probe_build")
file(REMOVE_RECURSE "${_per_tu_source_dir}" "${_per_tu_build_dir}")
file(MAKE_DIRECTORY "${_per_tu_source_dir}")
file(WRITE "${_per_tu_source_dir}/cases.cpp" [=[
#include "gentest/assertions.h"

[[using gentest: test("per_tu_warning_probe/builds")]]
void perTuWarningProbeBuilds() {
    gentest::asserts::EXPECT_TRUE(true);
}
]=])
file(WRITE "${_per_tu_source_dir}/CMakeLists.txt.in" [=[
cmake_minimum_required(VERSION 3.31)
project(gentest_per_tu_warning_probe LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
set(gentest_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(GENTEST_BUILD_CODEGEN OFF CACHE BOOL "" FORCE)
set(GENTEST_ENABLE_PUBLIC_MODULES OFF CACHE STRING "" FORCE)

add_subdirectory("@_gentest_source_dir_for_cmake@" gentest)

add_executable(per_tu_warning_probe)
target_link_libraries(per_tu_warning_probe PRIVATE gentest::gentest_main)
target_compile_features(per_tu_warning_probe PRIVATE cxx_std_20)

gentest_attach_codegen(per_tu_warning_probe
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
    SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/cases.cpp")
]=])
configure_file("${_per_tu_source_dir}/CMakeLists.txt.in" "${_per_tu_source_dir}/CMakeLists.txt" @ONLY)

_gentest_expect_success_forbids(
  "cmake per-tu output has no legacy manifest warning"
  "${_cmake_legacy_warning}"
  "${CMAKE_COMMAND}"
  ${_cmake_gen_args}
  -S "${_per_tu_source_dir}"
  -B "${_per_tu_build_dir}"
  ${_cmake_cache_args})

set(_per_tu_build_args --build "${_per_tu_build_dir}" --target per_tu_warning_probe)
if(DEFINED BUILD_CONFIG AND NOT "${BUILD_CONFIG}" STREQUAL "")
  list(APPEND _per_tu_build_args --config "${BUILD_CONFIG}")
elseif(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _per_tu_build_args --config "${BUILD_TYPE}")
endif()

gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" ${_per_tu_build_args}
  STRIP_TRAILING_WHITESPACE
  WORKING_DIRECTORY "${BUILD_ROOT}")
