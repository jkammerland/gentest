if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckMockManifestSplit.cmake: PROG not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckMockManifestSplit.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckMockManifestSplit.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED CODEGEN_STD OR "${CODEGEN_STD}" STREQUAL "")
  message(FATAL_ERROR "CheckMockManifestSplit.cmake: CODEGEN_STD not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

set(_work_dir "${BUILD_ROOT}/mock_manifest_split")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

set(_fixture_header "${_work_dir}/mock_manifest_fixture.hpp")
set(_fixture_source "${_work_dir}/mock_manifest_defs.cpp")
set(_manifest "${_work_dir}/service.mock_manifest.json")

file(WRITE "${_fixture_header}" [=[
#pragma once

namespace mock_manifest_split {

struct Service {
    virtual ~Service() = default;
    virtual int value(int input) const = 0;
    void reset() noexcept {}
};

} // namespace mock_manifest_split
]=])

file(WRITE "${_fixture_source}" [=[
#include "gentest/mock.h"
#include "mock_manifest_fixture.hpp"

namespace mock_manifest_split {

using ServiceMock = gentest::mock<Service>;

void use_mock_surface() {
    ServiceMock* service = nullptr;
    (void)service;
}

} // namespace mock_manifest_split
]=])

set(_clang_args)
if(DEFINED TARGET_ARG AND NOT "${TARGET_ARG}" STREQUAL "")
  list(APPEND _clang_args "${TARGET_ARG}")
endif()
gentest_make_public_api_include_args(
  _public_include_args
  SOURCE_ROOT "${SOURCE_DIR}"
  APPLE_SYSROOT)
list(APPEND _clang_args "${CODEGEN_STD}" ${_public_include_args} "-I${_work_dir}")

set(_unexpected_registry "${_work_dir}/unexpected_mock_registry.hpp")
set(_unexpected_artifact_manifest "${_work_dir}/unexpected_artifact_manifest.json")
execute_process(
  COMMAND "${PROG}"
    --discover-mocks
    --mock-manifest-output "${_manifest}"
    --artifact-manifest "${_unexpected_artifact_manifest}"
    "${_fixture_source}"
    --
    ${_clang_args}
  RESULT_VARIABLE _invalid_artifact_manifest_rc
  OUTPUT_VARIABLE _invalid_artifact_manifest_out
  ERROR_VARIABLE _invalid_artifact_manifest_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
if(NOT _invalid_artifact_manifest_rc EQUAL 1)
  message(FATAL_ERROR
    "Discovery-only mock manifest command should reject artifact manifests.\n"
    "--- stdout ---\n${_invalid_artifact_manifest_out}\n--- stderr ---\n${_invalid_artifact_manifest_err}")
endif()
string(FIND "${_invalid_artifact_manifest_out}\n${_invalid_artifact_manifest_err}"
  "--mock-manifest-output without --output/--tu-out-dir cannot emit artifact manifests"
  _invalid_artifact_manifest_msg_pos)
if(_invalid_artifact_manifest_msg_pos EQUAL -1)
  message(FATAL_ERROR
    "Discovery-only mock manifest command emitted the wrong artifact-manifest diagnostic.\n"
    "--- stdout ---\n${_invalid_artifact_manifest_out}\n--- stderr ---\n${_invalid_artifact_manifest_err}")
endif()

execute_process(
  COMMAND "${PROG}"
    --discover-mocks
    --mock-manifest-output "${_manifest}"
    "${_fixture_source}"
    --
    ${_clang_args}
  RESULT_VARIABLE _discover_rc
  OUTPUT_VARIABLE _discover_out
  ERROR_VARIABLE _discover_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
if(NOT _discover_rc EQUAL 0)
  message(FATAL_ERROR
    "Mock manifest discovery failed.\n"
    "--- stdout ---\n${_discover_out}\n--- stderr ---\n${_discover_err}")
endif()
if(NOT EXISTS "${_manifest}")
  message(FATAL_ERROR "Expected mock manifest '${_manifest}'")
endif()
if(EXISTS "${_unexpected_registry}")
  message(FATAL_ERROR "Discovery-only mock manifest command unexpectedly wrote '${_unexpected_registry}'")
endif()

file(READ "${_manifest}" _manifest_json)
string(JSON _manifest_schema GET "${_manifest_json}" schema)
string(JSON _manifest_mock_count LENGTH "${_manifest_json}" mocks)
string(JSON _manifest_mock_name GET "${_manifest_json}" mocks 0 qualified_name)
string(JSON _manifest_mock_kind GET "${_manifest_json}" mocks 0 definition_kind)
string(JSON _manifest_ctor_count LENGTH "${_manifest_json}" mocks 0 constructors)
string(JSON _manifest_method_count LENGTH "${_manifest_json}" mocks 0 methods)
string(JSON _manifest_method_name GET "${_manifest_json}" mocks 0 methods 0 method_name)
string(JSON _manifest_method_return GET "${_manifest_json}" mocks 0 methods 0 return_type)
foreach(_actual_expected IN ITEMS
    "_manifest_schema=gentest.mock_manifest.v1"
    "_manifest_mock_count=1"
    "_manifest_mock_name=mock_manifest_split::Service"
    "_manifest_mock_kind=header_like"
    "_manifest_ctor_count=0"
    "_manifest_method_count=2"
    "_manifest_method_name=reset"
    "_manifest_method_return=void")
  string(REPLACE "=" ";" _pair "${_actual_expected}")
  list(GET _pair 0 _actual_var)
  list(GET _pair 1 _expected_value)
  if(NOT "${${_actual_var}}" STREQUAL "${_expected_value}")
    message(FATAL_ERROR "Manifest mismatch for ${_actual_var}: expected '${_expected_value}', got '${${_actual_var}}'.\n${_manifest_json}")
  endif()
endforeach()

set(_registry "${_work_dir}/split_mock_registry.hpp")
set(_impl "${_work_dir}/split_mock_impl.hpp")
set(_domain_registry "${_work_dir}/split_mock_registry__domain_0000_header.hpp")
set(_domain_impl "${_work_dir}/split_mock_impl__domain_0000_header.hpp")

execute_process(
  COMMAND "${PROG}"
    --mock-manifest-input "${_manifest}"
    --tu-header-output "${_work_dir}/ignored.gentest.h"
    --mock-registry "${_registry}"
    --mock-impl "${_impl}"
    --mock-domain-registry-output "${_domain_registry}"
    --mock-domain-impl-output "${_domain_impl}"
  RESULT_VARIABLE _invalid_tu_option_rc
  OUTPUT_VARIABLE _invalid_tu_option_out
  ERROR_VARIABLE _invalid_tu_option_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
if(NOT _invalid_tu_option_rc EQUAL 1)
  message(FATAL_ERROR
    "Mock manifest emission should reject source/TU planning options.\n"
    "--- stdout ---\n${_invalid_tu_option_out}\n--- stderr ---\n${_invalid_tu_option_err}")
endif()
string(FIND "${_invalid_tu_option_out}\n${_invalid_tu_option_err}"
  "--mock-manifest-input cannot be combined with source/TU planning options"
  _invalid_tu_option_msg_pos)
if(_invalid_tu_option_msg_pos EQUAL -1)
  message(FATAL_ERROR
    "Mock manifest emission emitted the wrong source/TU planning diagnostic.\n"
    "--- stdout ---\n${_invalid_tu_option_out}\n--- stderr ---\n${_invalid_tu_option_err}")
endif()

execute_process(
  COMMAND "${PROG}"
    --mock-manifest-input "${_manifest}"
    --mock-registry "${_registry}"
    --mock-impl "${_impl}"
    --mock-domain-registry-output "${_domain_registry}"
    --mock-domain-impl-output "${_domain_impl}"
  RESULT_VARIABLE _emit_rc
  OUTPUT_VARIABLE _emit_out
  ERROR_VARIABLE _emit_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
if(NOT _emit_rc EQUAL 0)
  message(FATAL_ERROR
    "Mock manifest emission failed.\n"
    "--- stdout ---\n${_emit_out}\n--- stderr ---\n${_emit_err}")
endif()

foreach(_generated IN ITEMS "${_registry}" "${_impl}" "${_domain_registry}" "${_domain_impl}")
  if(NOT EXISTS "${_generated}")
    message(FATAL_ERROR "Expected generated mock output '${_generated}'")
  endif()
endforeach()

file(READ "${_registry}" _registry_text)
get_filename_component(_domain_registry_name "${_domain_registry}" NAME)
string(FIND "${_registry_text}" "#include \"${_domain_registry_name}\"" _aggregate_include_pos)
if(_aggregate_include_pos EQUAL -1)
  message(FATAL_ERROR "Expected aggregate registry to include '${_domain_registry_name}'.\n${_registry_text}")
endif()

file(READ "${_impl}" _impl_text)
get_filename_component(_domain_impl_name "${_domain_impl}" NAME)
string(FIND "${_impl_text}" "#include \"${_domain_impl_name}\"" _aggregate_impl_include_pos)
if(_aggregate_impl_include_pos EQUAL -1)
  message(FATAL_ERROR "Expected aggregate impl to include '${_domain_impl_name}'.\n${_impl_text}")
endif()

file(READ "${_domain_registry}" _domain_registry_text)
foreach(_token IN ITEMS "mock_manifest_fixture.hpp" "mock_manifest_split::Service" "MockAccess")
  string(FIND "${_domain_registry_text}" "${_token}" _domain_registry_token_pos)
  if(_domain_registry_token_pos EQUAL -1)
    message(FATAL_ERROR "Expected registry domain token '${_token}'.\n${_domain_registry_text}")
  endif()
endforeach()

file(READ "${_domain_impl}" _domain_impl_text)
foreach(_token IN ITEMS "mock_manifest_split::Service" "value" "dispatch_with_fallback")
  string(FIND "${_domain_impl_text}" "${_token}" _domain_impl_token_pos)
  if(_domain_impl_token_pos EQUAL -1)
    message(FATAL_ERROR "Expected impl domain token '${_token}'.\n${_domain_impl_text}")
  endif()
endforeach()

message(STATUS "Mock manifest split discovery/emission regression passed")
