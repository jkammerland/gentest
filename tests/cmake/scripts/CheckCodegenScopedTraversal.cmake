# Requires:
#  -DPROG=<path to gentest_codegen>
#  -DBUILD_ROOT=<build tree root>
#  -DSOURCE_DIR=<project source root>
#  -DCODEGEN_STD=<std flag, e.g. -std=c++23>
# Optional:
#  -DTARGET_ARG=<--target=...>

if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenScopedTraversal.cmake: PROG not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenScopedTraversal.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenScopedTraversal.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED CODEGEN_STD OR "${CODEGEN_STD}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenScopedTraversal.cmake: CODEGEN_STD not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

set(_work_dir "${BUILD_ROOT}/codegen_scoped_traversal")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

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
list(APPEND _clang_args "${_codegen_std}" ${_public_include_args} "-I${_work_dir}")

set(_tu_local_dir "${_work_dir}/tu_local")
file(MAKE_DIRECTORY "${_tu_local_dir}")
set(_tu_local_source "${_tu_local_dir}/local_class.cpp")
file(WRITE "${_tu_local_source}" [=[
#include "gentest/mock.h"

static int local_class_mock_use() {
    struct Local {
        void g() {}
    };
    using LocalMock = gentest::mock<Local>;
    return 0;
}
]=])

execute_process(
  COMMAND "${PROG}"
    --discover-mocks
    --tu-out-dir "${_tu_local_dir}/generated"
    --tu-header-output "${_tu_local_dir}/generated/tu_0000_local_class.gentest.h"
    --mock-registry "${_tu_local_dir}/mock_registry.hpp"
    --mock-impl "${_tu_local_dir}/mock_impl.hpp"
    --mock-domain-registry-output "${_tu_local_dir}/mock_registry__domain_0000_header.hpp"
    --mock-domain-impl-output "${_tu_local_dir}/mock_impl__domain_0000_header.hpp"
    "${_tu_local_source}"
    --
    ${_clang_args}
  RESULT_VARIABLE _tu_local_rc
  OUTPUT_VARIABLE _tu_local_out
  ERROR_VARIABLE _tu_local_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
if(NOT _tu_local_rc EQUAL 1)
  message(FATAL_ERROR
    "TU-mode mock discovery should reject local-class mocks.\n"
    "--- stdout ---\n${_tu_local_out}\n--- stderr ---\n${_tu_local_err}")
endif()
string(FIND "${_tu_local_out}\n${_tu_local_err}" "cannot mock a local class" _tu_local_msg_pos)
if(_tu_local_msg_pos EQUAL -1)
  message(FATAL_ERROR
    "TU-mode local-class mock diagnostic was not preserved.\n"
    "--- stdout ---\n${_tu_local_out}\n--- stderr ---\n${_tu_local_err}")
endif()

set(_specialization_dir "${_work_dir}/explicit_specialization")
file(MAKE_DIRECTORY "${_specialization_dir}")
set(_specialization_header "${_specialization_dir}/service.hpp")
set(_specialization_source "${_specialization_dir}/specialization.cpp")
set(_specialization_manifest "${_specialization_dir}/specialization.mock_manifest.json")
file(WRITE "${_specialization_header}" [=[
#pragma once

namespace scoped_traversal {

struct Service {
    virtual ~Service() = default;
    virtual int value() const = 0;
};

} // namespace scoped_traversal
]=])
file(WRITE "${_specialization_source}" [=[
#include "gentest/mock.h"
#include "service.hpp"

template <typename T>
struct Holder {};

template <>
struct Holder<int> {
    using ServiceMock = gentest::mock<scoped_traversal::Service>;
};
]=])

execute_process(
  COMMAND "${PROG}"
    --discover-mocks
    --mock-manifest-output "${_specialization_manifest}"
    "${_specialization_source}"
    --
    ${_clang_args}
  RESULT_VARIABLE _specialization_rc
  OUTPUT_VARIABLE _specialization_out
  ERROR_VARIABLE _specialization_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
if(NOT _specialization_rc EQUAL 0)
  message(FATAL_ERROR
    "Mock discovery failed for alias inside explicit specialization.\n"
    "--- stdout ---\n${_specialization_out}\n--- stderr ---\n${_specialization_err}")
endif()
if(NOT EXISTS "${_specialization_manifest}")
  message(FATAL_ERROR "Expected mock manifest '${_specialization_manifest}'")
endif()
file(READ "${_specialization_manifest}" _specialization_json)
string(JSON _specialization_mock_count LENGTH "${_specialization_json}" mocks)
string(JSON _specialization_mock_name GET "${_specialization_json}" mocks 0 qualified_name)
if(NOT "${_specialization_mock_count}" STREQUAL "1"
   OR NOT "${_specialization_mock_name}" STREQUAL "scoped_traversal::Service")
  message(FATAL_ERROR
    "Mock alias inside explicit specialization was not discovered.\n"
    "Expected one mock for scoped_traversal::Service.\n"
    "Manifest:\n${_specialization_json}")
endif()
