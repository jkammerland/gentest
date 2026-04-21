if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckLegacyManifestModeRemoved.cmake: PROG not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckLegacyManifestModeRemoved.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckLegacyManifestModeRemoved.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckLegacyManifestModeRemoved.cmake: GENTEST_SOURCE_DIR not set")
endif()
if(NOT DEFINED CODEGEN_STD OR "${CODEGEN_STD}" STREQUAL "")
  message(FATAL_ERROR "CheckLegacyManifestModeRemoved.cmake: CODEGEN_STD not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

set(_output_removed "legacy manifest/single-TU mode was removed in gentest 2.0.0")
set(_cmake_output_removed "OUTPUT manifest/single-TU mode")
set(_no_include_removed "--no-include-sources/GENTEST_NO_INCLUDE_SOURCES was removed")
set(_cmake_no_include_removed "NO_INCLUDE_SOURCES was removed")

function(_gentest_expect_failure label)
  set(one_value_args CONTAINS)
  set(multi_value_args COMMAND)
  cmake_parse_arguments(EXPECT "" "${one_value_args}" "${multi_value_args}" ${ARGN})
  if(NOT EXPECT_COMMAND)
    message(FATAL_ERROR "_gentest_expect_failure requires COMMAND")
  endif()
  if(NOT DEFINED EXPECT_CONTAINS)
    message(FATAL_ERROR "_gentest_expect_failure requires CONTAINS")
  endif()

  execute_process(
    COMMAND ${EXPECT_COMMAND}
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)

  set(_combined "${_out}\n${_err}")
  if(_rc EQUAL 0)
    message(FATAL_ERROR
      "${label}: expected failure, got success.\n"
      "--- stdout ---\n${_out}\n--- stderr ---\n${_err}")
  endif()

  string(FIND "${_combined}" "${EXPECT_CONTAINS}" _required_pos)
  if(_required_pos EQUAL -1)
    message(FATAL_ERROR
      "${label}: expected to find '${EXPECT_CONTAINS}'.\n"
      "--- stdout ---\n${_out}\n--- stderr ---\n${_err}")
  endif()
endfunction()

set(_compdb_root "${BUILD_ROOT}")
if(DEFINED COMPDB_ROOT AND NOT "${COMPDB_ROOT}" STREQUAL "")
  set(_compdb_root "${COMPDB_ROOT}")
endif()

set(_smoke_source "${SOURCE_DIR}/tests/smoke/codegen_axis_generators.cpp")
if(NOT EXISTS "${_smoke_source}")
  message(FATAL_ERROR "CheckLegacyManifestModeRemoved.cmake: missing smoke source '${_smoke_source}'")
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
gentest_normalize_std_flag_for_compiler(_codegen_std "clang++" "${CODEGEN_STD}")
list(APPEND _clang_args "${_codegen_std}" ${_public_include_args})

_gentest_expect_failure(
  "cli legacy manifest output removed"
  CONTAINS "${_output_removed}"
  COMMAND
    "${PROG}"
    --output "${BUILD_ROOT}/cli_legacy_manifest.cpp"
    --compdb "${_compdb_root}"
    "${_smoke_source}"
    --
    ${_clang_args})

_gentest_expect_failure(
  "cli no-include-sources option removed"
  CONTAINS "${_no_include_removed}"
  COMMAND
    "${PROG}"
    --check
    --no-include-sources
    --compdb "${_compdb_root}"
    "${_smoke_source}"
    --
    ${_clang_args})

_gentest_expect_failure(
  "cli no-include-sources env removed"
  CONTAINS "${_no_include_removed}"
  COMMAND
    "${CMAKE_COMMAND}"
    -E
    env
    GENTEST_NO_INCLUDE_SOURCES=1
    "${PROG}"
    --check
    --compdb "${_compdb_root}"
    "${_smoke_source}"
    --
    ${_clang_args})

function(_gentest_write_removed_option_fixture source_dir mode)
  file(REMOVE_RECURSE "${source_dir}")
  file(MAKE_DIRECTORY "${source_dir}")
  file(WRITE "${source_dir}/main.cpp" "int main() { return 0; }\n")
  file(WRITE "${source_dir}/cases.cpp"
    "#include \"gentest/assertions.h\"\n"
    "[[using gentest: test(\"removed/options\")]]\n"
    "void removed_options_case() { gentest::asserts::EXPECT_TRUE(true); }\n")

  if("${mode}" STREQUAL "OUTPUT")
    set(_fixture_codegen_call
      "gentest_attach_codegen(removed_options OUTPUT \"\${CMAKE_CURRENT_BINARY_DIR}/removed_options.gentest.cpp\" SOURCES \"\${CMAKE_CURRENT_SOURCE_DIR}/cases.cpp\")\n")
  elseif("${mode}" STREQUAL "NO_INCLUDE_SOURCES")
    set(_fixture_codegen_call
      "gentest_attach_codegen(removed_options NO_INCLUDE_SOURCES SOURCES \"\${CMAKE_CURRENT_SOURCE_DIR}/cases.cpp\")\n")
  else()
    message(FATAL_ERROR "Unknown removed-option fixture mode '${mode}'")
  endif()

  file(TO_CMAKE_PATH "${GENTEST_SOURCE_DIR}" _gentest_source_dir_for_cmake)
  file(WRITE "${source_dir}/CMakeLists.txt"
    "cmake_minimum_required(VERSION 3.31)\n"
    "project(gentest_removed_options LANGUAGES CXX)\n"
    "include(\"${_gentest_source_dir_for_cmake}/cmake/GentestCodegen.cmake\")\n"
    "set(GENTEST_CODEGEN_EXECUTABLE \"${CMAKE_COMMAND}\")\n"
    "add_executable(removed_options \"\${CMAKE_CURRENT_SOURCE_DIR}/main.cpp\")\n"
    "${_fixture_codegen_call}")
endfunction()

function(_gentest_expect_configure_failure source_dir label expected)
  set(_build_dir "${source_dir}-build")
  file(REMOVE_RECURSE "${_build_dir}")
  set(_cmd "${CMAKE_COMMAND}" -S "${source_dir}" -B "${_build_dir}" -G "${GENERATOR}")
  if(DEFINED GENERATOR_PLATFORM AND NOT "${GENERATOR_PLATFORM}" STREQUAL "")
    list(APPEND _cmd -A "${GENERATOR_PLATFORM}")
  endif()
  if(DEFINED GENERATOR_TOOLSET AND NOT "${GENERATOR_TOOLSET}" STREQUAL "")
    list(APPEND _cmd -T "${GENERATOR_TOOLSET}")
  endif()
  if(DEFINED TOOLCHAIN_FILE AND NOT "${TOOLCHAIN_FILE}" STREQUAL "")
    list(APPEND _cmd "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}")
  endif()
  if(DEFINED MAKE_PROGRAM AND NOT "${MAKE_PROGRAM}" STREQUAL "")
    list(APPEND _cmd "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
  endif()
  if(DEFINED C_COMPILER AND NOT "${C_COMPILER}" STREQUAL "")
    list(APPEND _cmd "-DCMAKE_C_COMPILER=${C_COMPILER}")
  endif()
  if(DEFINED CXX_COMPILER AND NOT "${CXX_COMPILER}" STREQUAL "")
    list(APPEND _cmd "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}")
  endif()
  if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
    list(APPEND _cmd "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
  endif()

  _gentest_expect_failure("${label}" CONTAINS "${expected}" COMMAND ${_cmd})
endfunction()

set(_output_fixture "${BUILD_ROOT}/cmake_removed_output_src")
_gentest_write_removed_option_fixture("${_output_fixture}" OUTPUT)
_gentest_expect_configure_failure("${_output_fixture}" "cmake OUTPUT removed" "${_cmake_output_removed}")

set(_no_include_fixture "${BUILD_ROOT}/cmake_removed_no_include_sources_src")
_gentest_write_removed_option_fixture("${_no_include_fixture}" NO_INCLUDE_SOURCES)
_gentest_expect_configure_failure("${_no_include_fixture}" "cmake NO_INCLUDE_SOURCES removed" "${_cmake_no_include_removed}")

message(STATUS "Legacy manifest and no-include-sources removal checks passed")
