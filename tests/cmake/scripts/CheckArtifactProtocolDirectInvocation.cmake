if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckArtifactProtocolDirectInvocation.cmake: PROG not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckArtifactProtocolDirectInvocation.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckArtifactProtocolDirectInvocation.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED CODEGEN_STD OR "${CODEGEN_STD}" STREQUAL "")
  message(FATAL_ERROR "CheckArtifactProtocolDirectInvocation.cmake: CODEGEN_STD not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

set(_work_dir "${BUILD_ROOT}/artifact_protocol_direct_invocation")
set(_source_dir "${_work_dir}/src")
set(_generated_dir "${_work_dir}/generated")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_source_dir}" "${_generated_dir}")

set(_owner_source "${_source_dir}/cases.cpp")
set(_run_marker "${_work_dir}/direct_textual.ran")
file(TO_CMAKE_PATH "${_run_marker}" _run_marker_cxx)
file(WRITE "${_owner_source}" [=[
#include <fstream>

#include "gentest/attributes.h"
#include "gentest/runner.h"

using namespace gentest::asserts;

namespace {
int localValue() { return 42; }
} // namespace

[[using gentest: test("protocol/direct_textual")]]
static void directTextualCase() {
    std::ofstream marker{"]=] "${_run_marker_cxx}" [=["};
    marker << "ran\n";
    EXPECT_EQ(localValue(), 42);
}
]=])

set(_wrapper_source "${_generated_dir}/tu_0000_cases.gentest.cpp")
set(_header "${_generated_dir}/tu_0000_cases.gentest.h")
set(_manifest "${_generated_dir}/direct_textual.artifact_manifest.json")
set(_depfile "${_generated_dir}/direct_textual.d")
set(_validation_stamp "${_generated_dir}/direct_textual.artifact_manifest.validated")
file(TO_CMAKE_PATH "${_owner_source}" _owner_source_include)
file(WRITE "${_wrapper_source}" [=[
// This file is written by the buildsystem adapter before gentest_codegen runs.
// NOLINTNEXTLINE(bugprone-suspicious-include)
#include "]=] "${_owner_source_include}" [=["

#if !defined(GENTEST_CODEGEN) && __has_include("tu_0000_cases.gentest.h")
#include "tu_0000_cases.gentest.h"
#endif
]=])

set(_clang_args)
if(DEFINED TARGET_ARG AND NOT "${TARGET_ARG}" STREQUAL "")
  list(APPEND _clang_args "${TARGET_ARG}")
endif()
set(_codegen_host_compiler "clang++")
if(DEFINED ENV{GENTEST_CODEGEN_HOST_CLANG} AND NOT "$ENV{GENTEST_CODEGEN_HOST_CLANG}" STREQUAL "")
  set(_codegen_host_compiler "$ENV{GENTEST_CODEGEN_HOST_CLANG}")
endif()
gentest_normalize_std_flag_for_compiler(_codegen_std "${_codegen_host_compiler}" "${CODEGEN_STD}")
gentest_make_public_api_include_args(
  _public_include_args
  SOURCE_ROOT "${SOURCE_DIR}"
  APPLE_SYSROOT)
list(APPEND _clang_args "${_codegen_std}" ${_public_include_args} "-I${_source_dir}" "-DGENTEST_CODEGEN=1")

gentest_check_run_or_fail(
  COMMAND "${PROG}"
    --tu-out-dir "${_generated_dir}"
    --tu-header-output "${_header}"
    --artifact-owner-source "${_owner_source}"
    --artifact-manifest "${_manifest}"
    --compile-context-id "direct_textual:${_owner_source}"
    --depfile "${_depfile}"
    "${_wrapper_source}"
    --
    ${_clang_args}
  STRIP_TRAILING_WHITESPACE)

foreach(_expected_file IN ITEMS "${_header}" "${_manifest}" "${_depfile}")
  if(NOT EXISTS "${_expected_file}")
    message(FATAL_ERROR "Expected direct protocol output '${_expected_file}'")
  endif()
endforeach()

file(READ "${_header}" _header_text)
string(FIND "${_header_text}" "protocol/direct_textual" _case_name_pos)
if(_case_name_pos EQUAL -1)
  message(FATAL_ERROR "Generated direct textual header is missing the discovered case.\n${_header_text}")
endif()

file(READ "${_manifest}" _manifest_json)
string(JSON _manifest_schema GET "${_manifest_json}" schema)
string(JSON _source_count LENGTH "${_manifest_json}" sources)
string(JSON _artifact_count LENGTH "${_manifest_json}" artifacts)
string(JSON _source_path GET "${_manifest_json}" sources 0 source)
string(JSON _source_kind GET "${_manifest_json}" sources 0 kind)
string(JSON _source_owner GET "${_manifest_json}" sources 0 owner_source)
string(JSON _source_header GET "${_manifest_json}" sources 0 registration_header)
string(JSON _artifact_path GET "${_manifest_json}" artifacts 0 path)
string(JSON _artifact_compile_as GET "${_manifest_json}" artifacts 0 compile_as)
string(JSON _artifact_attachment GET "${_manifest_json}" artifacts 0 target_attachment)
string(JSON _artifact_owner GET "${_manifest_json}" artifacts 0 owner_source)
string(JSON _artifact_scan GET "${_manifest_json}" artifacts 0 requires_module_scan)
string(JSON _artifact_includes_owner GET "${_manifest_json}" artifacts 0 includes_owner_source)
string(JSON _artifact_replaces_owner GET "${_manifest_json}" artifacts 0 replaces_owner_source)
string(JSON _artifact_header GET "${_manifest_json}" artifacts 0 generated_headers 0)
string(JSON _artifact_depfile GET "${_manifest_json}" artifacts 0 depfile)

foreach(_actual_expected IN ITEMS
    "_manifest_schema=gentest.artifact_manifest.v1"
    "_source_count=1"
    "_artifact_count=1"
    "_source_path=${_wrapper_source}"
    "_source_kind=textual-wrapper"
    "_source_owner=${_owner_source}"
    "_source_header=${_header}"
    "_artifact_path=${_wrapper_source}"
    "_artifact_compile_as=cxx-textual-wrapper"
    "_artifact_attachment=replace-owner-source"
    "_artifact_owner=${_owner_source}"
    "_artifact_scan=OFF"
    "_artifact_includes_owner=ON"
    "_artifact_replaces_owner=ON"
    "_artifact_header=${_header}"
    "_artifact_depfile=${_depfile}")
  string(REPLACE "=" ";" _pair "${_actual_expected}")
  list(GET _pair 0 _actual_var)
  list(GET _pair 1 _expected_value)
  if(NOT "${${_actual_var}}" STREQUAL "${_expected_value}")
    message(FATAL_ERROR
      "Direct artifact manifest mismatch for ${_actual_var}: expected '${_expected_value}', got '${${_actual_var}}'.\n"
      "${_manifest_json}")
  endif()
endforeach()

set(_validator_args
  "${PROG}" validate-artifact-manifest
  --manifest "${_manifest}"
  --stamp "${_validation_stamp}"
  --expected-source "${_wrapper_source}"
  --expected-source-kind textual-wrapper
  --expected-registration-output "${_wrapper_source}"
  --expected-header "${_header}"
  --expected-compile-context-id "direct_textual:${_owner_source}"
  --expected-owner-source "${_owner_source}"
  --expected-include-dir "${_generated_dir}"
  --expected-depfile "${_depfile}"
  --expected-target-attachment replace-owner-source
  --expected-artifact-role registration
  --expected-compile-as cxx-textual-wrapper
  --expected-requires-module-scan OFF
  --expected-includes-owner-source ON
  --expected-replaces-owner-source ON)
gentest_check_run_or_fail(
  COMMAND ${_validator_args}
  STRIP_TRAILING_WHITESPACE)
if(NOT EXISTS "${_validation_stamp}")
  message(FATAL_ERROR "Expected artifact manifest validator to touch '${_validation_stamp}'")
endif()

set(_consumer_dir "${_work_dir}/consumer")
set(_consumer_build_dir "${_work_dir}/consumer-build")
file(MAKE_DIRECTORY "${_consumer_dir}")
file(WRITE "${_consumer_dir}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.28)
project(gentest_artifact_protocol_direct_consumer LANGUAGES CXX)

set(gentest_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(GENTEST_BUILD_CODEGEN OFF CACHE BOOL "" FORCE)
add_subdirectory("@GENTEST_SOURCE_ROOT@" gentest-src EXCLUDE_FROM_ALL)

add_executable(protocol_direct "@GENTEST_WRAPPER_SOURCE@")
target_include_directories(protocol_direct PRIVATE "@GENTEST_GENERATED_DIR@")
target_link_libraries(protocol_direct PRIVATE gentest::gentest_main)

enable_testing()
add_test(NAME protocol_direct COMMAND protocol_direct --run=protocol/direct_textual)
]=])
file(READ "${_consumer_dir}/CMakeLists.txt" _consumer_cmake)
string(REPLACE "@GENTEST_SOURCE_ROOT@" "${SOURCE_DIR}" _consumer_cmake "${_consumer_cmake}")
string(REPLACE "@GENTEST_WRAPPER_SOURCE@" "${_wrapper_source}" _consumer_cmake "${_consumer_cmake}")
string(REPLACE "@GENTEST_GENERATED_DIR@" "${_generated_dir}" _consumer_cmake "${_consumer_cmake}")
file(WRITE "${_consumer_dir}/CMakeLists.txt" "${_consumer_cmake}")

set(_consumer_configure_args
  -S "${_consumer_dir}"
  -B "${_consumer_build_dir}"
  -Dgentest_BUILD_TESTING=OFF
  -DGENTEST_BUILD_CODEGEN=OFF)
if(DEFINED GENERATOR AND NOT "${GENERATOR}" STREQUAL "")
  list(APPEND _consumer_configure_args -G "${GENERATOR}")
endif()
if(DEFINED GENERATOR_PLATFORM AND NOT "${GENERATOR_PLATFORM}" STREQUAL "")
  list(APPEND _consumer_configure_args -A "${GENERATOR_PLATFORM}")
endif()
if(DEFINED GENERATOR_TOOLSET AND NOT "${GENERATOR_TOOLSET}" STREQUAL "")
  list(APPEND _consumer_configure_args -T "${GENERATOR_TOOLSET}")
endif()
if(DEFINED TOOLCHAIN_FILE AND NOT "${TOOLCHAIN_FILE}" STREQUAL "")
  list(APPEND _consumer_configure_args "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}")
endif()
if(DEFINED MAKE_PROGRAM AND NOT "${MAKE_PROGRAM}" STREQUAL "")
  list(APPEND _consumer_configure_args "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()
if(DEFINED C_COMPILER AND NOT "${C_COMPILER}" STREQUAL "")
  list(APPEND _consumer_configure_args "-DCMAKE_C_COMPILER=${C_COMPILER}")
endif()
if(DEFINED CXX_COMPILER AND NOT "${CXX_COMPILER}" STREQUAL "")
  list(APPEND _consumer_configure_args "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}")
endif()
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _consumer_configure_args "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()
if(DEFINED LLVM_DIR AND NOT "${LLVM_DIR}" STREQUAL "")
  list(APPEND _consumer_configure_args "-DLLVM_DIR=${LLVM_DIR}")
endif()
if(DEFINED Clang_DIR AND NOT "${Clang_DIR}" STREQUAL "")
  list(APPEND _consumer_configure_args "-DClang_DIR=${Clang_DIR}")
endif()

gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" ${_consumer_configure_args}
  STRIP_TRAILING_WHITESPACE)
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_consumer_build_dir}" --target protocol_direct
  STRIP_TRAILING_WHITESPACE)
set(_consumer_ctest_args --test-dir "${_consumer_build_dir}" --output-on-failure)
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _consumer_ctest_args -C "${BUILD_TYPE}")
endif()
file(REMOVE "${_run_marker}")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_CTEST_COMMAND}" ${_consumer_ctest_args}
  STRIP_TRAILING_WHITESPACE)
if(NOT EXISTS "${_run_marker}")
  message(FATAL_ERROR "Expected direct textual test body to write '${_run_marker}'")
endif()
file(READ "${_run_marker}" _run_marker_text)
if(NOT "${_run_marker_text}" STREQUAL "ran\n")
  message(FATAL_ERROR "Direct textual test body marker has unexpected content: '${_run_marker_text}'")
endif()

set(_bad_manifest "${_generated_dir}/direct_textual.bad_schema.artifact_manifest.json")
string(JSON _bad_manifest_json SET "${_manifest_json}" schema "\"gentest.artifact_manifest.v0\"")
file(WRITE "${_bad_manifest}" "${_bad_manifest_json}")
set(_bad_validation_stamp "${_generated_dir}/direct_textual.bad_schema.artifact_manifest.validated")
set(_bad_validator_args
  "${PROG}" validate-artifact-manifest
  --manifest "${_bad_manifest}"
  --stamp "${_bad_validation_stamp}"
  --expected-source "${_wrapper_source}"
  --expected-source-kind textual-wrapper
  --expected-registration-output "${_wrapper_source}"
  --expected-header "${_header}"
  --expected-compile-context-id "direct_textual:${_owner_source}"
  --expected-owner-source "${_owner_source}"
  --expected-include-dir "${_generated_dir}"
  --expected-depfile "${_depfile}"
  --expected-target-attachment replace-owner-source
  --expected-artifact-role registration
  --expected-compile-as cxx-textual-wrapper
  --expected-requires-module-scan false
  --expected-includes-owner-source true
  --expected-replaces-owner-source 1)
execute_process(
  COMMAND ${_bad_validator_args}
  RESULT_VARIABLE _bad_schema_rc
  OUTPUT_VARIABLE _bad_schema_out
  ERROR_VARIABLE _bad_schema_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
if(NOT _bad_schema_rc EQUAL 1)
  message(FATAL_ERROR
    "Artifact manifest validator should reject unsupported schemas.\n"
    "--- stdout ---\n${_bad_schema_out}\n--- stderr ---\n${_bad_schema_err}")
endif()
if(EXISTS "${_bad_validation_stamp}")
  message(FATAL_ERROR "Artifact manifest validator touched '${_bad_validation_stamp}' despite rejecting the schema")
endif()
string(FIND "${_bad_schema_out}\n${_bad_schema_err}" "unsupported artifact manifest schema" _bad_schema_msg_pos)
if(_bad_schema_msg_pos EQUAL -1)
  message(FATAL_ERROR
    "Artifact manifest validator emitted the wrong schema diagnostic.\n"
    "--- stdout ---\n${_bad_schema_out}\n--- stderr ---\n${_bad_schema_err}")
endif()

message(STATUS "Direct artifact protocol invocation regression passed")
