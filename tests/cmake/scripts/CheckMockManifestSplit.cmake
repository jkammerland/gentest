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

execute_process(
  COMMAND "${PROG}"
    inspect-mocks
    "${_fixture_source}"
    --
    ${_clang_args}
  RESULT_VARIABLE _inspect_missing_manifest_rc
  OUTPUT_VARIABLE _inspect_missing_manifest_out
  ERROR_VARIABLE _inspect_missing_manifest_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
if(NOT _inspect_missing_manifest_rc EQUAL 1)
  message(FATAL_ERROR
    "inspect-mocks should require --mock-manifest-output.\n"
    "--- stdout ---\n${_inspect_missing_manifest_out}\n--- stderr ---\n${_inspect_missing_manifest_err}")
endif()
string(FIND "${_inspect_missing_manifest_out}\n${_inspect_missing_manifest_err}"
  "inspect-mocks requires --mock-manifest-output"
  _inspect_missing_manifest_msg_pos)
if(_inspect_missing_manifest_msg_pos EQUAL -1)
  message(FATAL_ERROR
    "inspect-mocks emitted the wrong missing-manifest diagnostic.\n"
    "--- stdout ---\n${_inspect_missing_manifest_out}\n--- stderr ---\n${_inspect_missing_manifest_err}")
endif()

execute_process(
  COMMAND "${PROG}"
    emit-mocks
    --mock-registry "${_work_dir}/missing_input_registry.hpp"
    --mock-impl "${_work_dir}/missing_input_impl.hpp"
  RESULT_VARIABLE _emit_missing_manifest_rc
  OUTPUT_VARIABLE _emit_missing_manifest_out
  ERROR_VARIABLE _emit_missing_manifest_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
if(NOT _emit_missing_manifest_rc EQUAL 1)
  message(FATAL_ERROR
    "emit-mocks should require --mock-manifest-input.\n"
    "--- stdout ---\n${_emit_missing_manifest_out}\n--- stderr ---\n${_emit_missing_manifest_err}")
endif()
string(FIND "${_emit_missing_manifest_out}\n${_emit_missing_manifest_err}"
  "emit-mocks requires --mock-manifest-input"
  _emit_missing_manifest_msg_pos)
if(_emit_missing_manifest_msg_pos EQUAL -1)
  message(FATAL_ERROR
    "emit-mocks emitted the wrong missing-manifest diagnostic.\n"
    "--- stdout ---\n${_emit_missing_manifest_out}\n--- stderr ---\n${_emit_missing_manifest_err}")
endif()

execute_process(
  COMMAND "${PROG}"
    inspect-mocks
    --mock-manifest-output "${_work_dir}/invalid_phase.mock_manifest.json"
    --mock-registry "${_work_dir}/invalid_phase_registry.hpp"
    "${_fixture_source}"
    --
    ${_clang_args}
  RESULT_VARIABLE _inspect_output_paths_rc
  OUTPUT_VARIABLE _inspect_output_paths_out
  ERROR_VARIABLE _inspect_output_paths_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
if(NOT _inspect_output_paths_rc EQUAL 1)
  message(FATAL_ERROR
    "inspect-mocks should reject final mock output paths.\n"
    "--- stdout ---\n${_inspect_output_paths_out}\n--- stderr ---\n${_inspect_output_paths_err}")
endif()
string(FIND "${_inspect_output_paths_out}\n${_inspect_output_paths_err}"
  "inspect-mocks cannot be combined with final mock output paths"
  _inspect_output_paths_msg_pos)
if(_inspect_output_paths_msg_pos EQUAL -1)
  message(FATAL_ERROR
    "inspect-mocks emitted the wrong final-output diagnostic.\n"
    "--- stdout ---\n${_inspect_output_paths_out}\n--- stderr ---\n${_inspect_output_paths_err}")
endif()

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
  "--mock-manifest-output without --tu-out-dir cannot emit artifact manifests"
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

set(_phase_manifest "${_work_dir}/service.phase.mock_manifest.json")
execute_process(
  COMMAND "${PROG}"
    inspect-mocks
    --mock-manifest-output "${_phase_manifest}"
    "${_fixture_source}"
    --
    ${_clang_args}
  RESULT_VARIABLE _phase_discover_rc
  OUTPUT_VARIABLE _phase_discover_out
  ERROR_VARIABLE _phase_discover_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
if(NOT _phase_discover_rc EQUAL 0)
  message(FATAL_ERROR
    "inspect-mocks discovery failed.\n"
    "--- stdout ---\n${_phase_discover_out}\n--- stderr ---\n${_phase_discover_err}")
endif()
if(NOT EXISTS "${_phase_manifest}")
  message(FATAL_ERROR "Expected inspect-mocks manifest '${_phase_manifest}'")
endif()
file(READ "${_phase_manifest}" _phase_manifest_json)
string(JSON _phase_manifest_schema GET "${_phase_manifest_json}" schema)
string(JSON _phase_manifest_domain_module_count LENGTH "${_phase_manifest_json}" mock_output_domain_modules)
string(JSON _phase_manifest_mock_count LENGTH "${_phase_manifest_json}" mocks)
string(JSON _phase_manifest_mock_name GET "${_phase_manifest_json}" mocks 0 qualified_name)
string(JSON _phase_manifest_mock_kind GET "${_phase_manifest_json}" mocks 0 definition_kind)
string(JSON _phase_manifest_ctor_count LENGTH "${_phase_manifest_json}" mocks 0 constructors)
string(JSON _phase_manifest_method_count LENGTH "${_phase_manifest_json}" mocks 0 methods)
string(JSON _phase_manifest_method_name GET "${_phase_manifest_json}" mocks 0 methods 0 method_name)
string(JSON _phase_manifest_method_return GET "${_phase_manifest_json}" mocks 0 methods 0 return_type)
foreach(_actual_expected IN ITEMS
    "_phase_manifest_schema=gentest.mock_manifest.v1"
    "_phase_manifest_domain_module_count=0"
    "_phase_manifest_mock_count=1"
    "_phase_manifest_mock_name=mock_manifest_split::Service"
    "_phase_manifest_mock_kind=header_like"
    "_phase_manifest_ctor_count=0"
    "_phase_manifest_method_count=2"
    "_phase_manifest_method_name=reset"
    "_phase_manifest_method_return=void")
  string(REPLACE "=" ";" _pair "${_actual_expected}")
  list(GET _pair 0 _actual_var)
  list(GET _pair 1 _expected_value)
  if(NOT "${${_actual_var}}" STREQUAL "${_expected_value}")
    message(FATAL_ERROR
      "inspect-mocks manifest mismatch for ${_actual_var}: expected '${_expected_value}', got '${${_actual_var}}'.\n"
      "${_phase_manifest_json}")
  endif()
endforeach()

file(READ "${_manifest}" _manifest_json)
string(JSON _manifest_schema GET "${_manifest_json}" schema)
string(JSON _manifest_domain_module_count LENGTH "${_manifest_json}" mock_output_domain_modules)
string(JSON _manifest_mock_count LENGTH "${_manifest_json}" mocks)
string(JSON _manifest_mock_name GET "${_manifest_json}" mocks 0 qualified_name)
string(JSON _manifest_mock_kind GET "${_manifest_json}" mocks 0 definition_kind)
string(JSON _manifest_ctor_count LENGTH "${_manifest_json}" mocks 0 constructors)
string(JSON _manifest_method_count LENGTH "${_manifest_json}" mocks 0 methods)
string(JSON _manifest_method_name GET "${_manifest_json}" mocks 0 methods 0 method_name)
string(JSON _manifest_method_return GET "${_manifest_json}" mocks 0 methods 0 return_type)
foreach(_actual_expected IN ITEMS
    "_manifest_schema=gentest.mock_manifest.v1"
    "_manifest_domain_module_count=0"
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

set(_bad_schema_manifest "${_work_dir}/service.bad_schema.mock_manifest.json")
string(JSON _bad_schema_manifest_json SET "${_manifest_json}" schema "\"gentest.mock_manifest.v0\"")
file(WRITE "${_bad_schema_manifest}" "${_bad_schema_manifest_json}")

execute_process(
  COMMAND "${PROG}"
    emit-mocks
    --mock-manifest-input "${_bad_schema_manifest}"
    --mock-registry "${_work_dir}/bad_schema_mock_registry.hpp"
    --mock-impl "${_work_dir}/bad_schema_mock_impl.hpp"
    --mock-domain-registry-output "${_work_dir}/bad_schema_mock_registry__domain_0000_header.hpp"
    --mock-domain-impl-output "${_work_dir}/bad_schema_mock_impl__domain_0000_header.hpp"
  RESULT_VARIABLE _bad_schema_emit_rc
  OUTPUT_VARIABLE _bad_schema_emit_out
  ERROR_VARIABLE _bad_schema_emit_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
if(NOT _bad_schema_emit_rc EQUAL 1)
  message(FATAL_ERROR
    "emit-mocks should reject unsupported mock manifest schemas.\n"
    "--- stdout ---\n${_bad_schema_emit_out}\n--- stderr ---\n${_bad_schema_emit_err}")
endif()
string(FIND "${_bad_schema_emit_out}\n${_bad_schema_emit_err}"
  "unsupported mock manifest schema"
  _bad_schema_emit_msg_pos)
if(_bad_schema_emit_msg_pos EQUAL -1)
  message(FATAL_ERROR
    "emit-mocks emitted the wrong schema diagnostic.\n"
    "--- stdout ---\n${_bad_schema_emit_out}\n--- stderr ---\n${_bad_schema_emit_err}")
endif()

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

set(_phase_registry "${_work_dir}/phase_mock_registry.hpp")
set(_phase_impl "${_work_dir}/phase_mock_impl.hpp")
set(_phase_domain_registry "${_work_dir}/phase_mock_registry__domain_0000_header.hpp")
set(_phase_domain_impl "${_work_dir}/phase_mock_impl__domain_0000_header.hpp")

execute_process(
  COMMAND "${PROG}"
    emit-mocks
    --mock-manifest-input "${_phase_manifest}"
    --mock-registry "${_phase_registry}"
    --mock-impl "${_phase_impl}"
    --mock-domain-registry-output "${_phase_domain_registry}"
    --mock-domain-impl-output "${_phase_domain_impl}"
  RESULT_VARIABLE _phase_emit_rc
  OUTPUT_VARIABLE _phase_emit_out
  ERROR_VARIABLE _phase_emit_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
if(NOT _phase_emit_rc EQUAL 0)
  message(FATAL_ERROR
    "emit-mocks emission failed.\n"
    "--- stdout ---\n${_phase_emit_out}\n--- stderr ---\n${_phase_emit_err}")
endif()
foreach(_generated IN ITEMS "${_phase_registry}" "${_phase_impl}" "${_phase_domain_registry}" "${_phase_domain_impl}")
  if(NOT EXISTS "${_generated}")
    message(FATAL_ERROR "Expected emit-mocks generated output '${_generated}'")
  endif()
endforeach()

file(READ "${_phase_registry}" _phase_registry_text)
get_filename_component(_phase_domain_registry_name "${_phase_domain_registry}" NAME)
string(FIND "${_phase_registry_text}" "#include \"${_phase_domain_registry_name}\"" _phase_aggregate_include_pos)
if(_phase_aggregate_include_pos EQUAL -1)
  message(FATAL_ERROR "Expected emit-mocks aggregate registry to include '${_phase_domain_registry_name}'.\n${_phase_registry_text}")
endif()

file(READ "${_phase_impl}" _phase_impl_text)
get_filename_component(_phase_domain_impl_name "${_phase_domain_impl}" NAME)
string(FIND "${_phase_impl_text}" "#include \"${_phase_domain_impl_name}\"" _phase_aggregate_impl_include_pos)
if(_phase_aggregate_impl_include_pos EQUAL -1)
  message(FATAL_ERROR "Expected emit-mocks aggregate impl to include '${_phase_domain_impl_name}'.\n${_phase_impl_text}")
endif()

file(READ "${_phase_domain_registry}" _phase_domain_registry_text)
foreach(_token IN ITEMS "mock_manifest_fixture.hpp" "mock_manifest_split::Service" "MockAccess")
  string(FIND "${_phase_domain_registry_text}" "${_token}" _phase_domain_registry_token_pos)
  if(_phase_domain_registry_token_pos EQUAL -1)
    message(FATAL_ERROR "Expected emit-mocks registry domain token '${_token}'.\n${_phase_domain_registry_text}")
  endif()
endforeach()

file(READ "${_phase_domain_impl}" _phase_domain_impl_text)
foreach(_token IN ITEMS "mock_manifest_split::Service" "value" "dispatch_with_fallback")
  string(FIND "${_phase_domain_impl_text}" "${_token}" _phase_domain_impl_token_pos)
  if(_phase_domain_impl_token_pos EQUAL -1)
    message(FATAL_ERROR "Expected emit-mocks impl domain token '${_token}'.\n${_phase_domain_impl_text}")
  endif()
endforeach()

set(_module_manifest "${_work_dir}/module.mock_manifest.json")
file(WRITE "${_module_manifest}" [=[
{
  "schema": "gentest.mock_manifest.v1",
  "mock_output_domain_modules": [
    "mock_manifest_split.module_service"
  ],
  "mocks": [
    {
      "qualified_name": "mock_manifest_split_module::Service",
      "display_name": "mock_manifest_split_module::Service",
      "definition_file": "module_service.cppm",
      "definition_kind": "named_module",
      "use_files": [
        "module_mock_defs.cppm"
      ],
      "definition_module_name": "mock_manifest_split.module_service",
      "attachment_namespace_chain": [],
      "derive_for_virtual": true,
      "has_accessible_default_ctor": false,
      "has_virtual_destructor": true,
      "constructors": [],
      "methods": [
        {
          "qualified_name": "mock_manifest_split_module::Service::value",
          "method_name": "value",
          "return_type": "int",
          "parameters": [
            {
              "type": "int",
              "name": "input",
              "pass_style": "value"
            }
          ],
          "template_prefix": "",
          "template_params": [],
          "is_static": false,
          "is_virtual": true,
          "is_pure_virtual": true,
          "qualifiers": {
            "cv": "const",
            "ref": "none",
            "is_noexcept": false
          }
        }
      ]
    }
  ]
}
]=])

set(_stale_module_manifest "${_work_dir}/module.stale_domain.mock_manifest.json")
file(READ "${_module_manifest}" _module_manifest_json)
string(REPLACE
  "\"mock_output_domain_modules\": [\n    \"mock_manifest_split.module_service\"\n  ]"
  "\"mock_output_domain_modules\": [\n    \"mock_manifest_split.other_module\"\n  ]"
  _stale_module_manifest_json
  "${_module_manifest_json}")
file(WRITE "${_stale_module_manifest}" "${_stale_module_manifest_json}")

set(_missing_domain_module_manifest "${_work_dir}/module.missing_domain.mock_manifest.json")
string(REPLACE
  "  \"mock_output_domain_modules\": [\n    \"mock_manifest_split.module_service\"\n  ],\n"
  ""
  _missing_domain_module_manifest_json
  "${_module_manifest_json}")
file(WRITE "${_missing_domain_module_manifest}" "${_missing_domain_module_manifest_json}")

execute_process(
  COMMAND "${PROG}"
    emit-mocks
    --mock-manifest-input "${_missing_domain_module_manifest}"
    --mock-registry "${_work_dir}/missing_domain_module_mock_registry.hpp"
    --mock-impl "${_work_dir}/missing_domain_module_mock_impl.hpp"
    --mock-domain-registry-output "${_work_dir}/missing_domain_module_mock_registry__domain_0000_header.hpp"
    --mock-domain-impl-output "${_work_dir}/missing_domain_module_mock_impl__domain_0000_header.hpp"
  RESULT_VARIABLE _missing_domain_module_emit_rc
  OUTPUT_VARIABLE _missing_domain_module_emit_out
  ERROR_VARIABLE _missing_domain_module_emit_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
if(NOT _missing_domain_module_emit_rc EQUAL 1)
  message(FATAL_ERROR
    "emit-mocks should reject named-module mocks without mock_output_domain_modules.\n"
    "--- stdout ---\n${_missing_domain_module_emit_out}\n--- stderr ---\n${_missing_domain_module_emit_err}")
endif()
string(FIND "${_missing_domain_module_emit_out}\n${_missing_domain_module_emit_err}"
  "mock manifest is missing 'mock_output_domain_modules'"
  _missing_domain_module_emit_msg_pos)
if(_missing_domain_module_emit_msg_pos EQUAL -1)
  message(FATAL_ERROR
    "emit-mocks emitted the wrong missing-domain diagnostic.\n"
    "--- stdout ---\n${_missing_domain_module_emit_out}\n--- stderr ---\n${_missing_domain_module_emit_err}")
endif()

execute_process(
  COMMAND "${PROG}"
    emit-mocks
    --mock-manifest-input "${_stale_module_manifest}"
    --mock-registry "${_work_dir}/stale_module_mock_registry.hpp"
    --mock-impl "${_work_dir}/stale_module_mock_impl.hpp"
    --mock-domain-registry-output "${_work_dir}/stale_module_mock_registry__domain_0000_header.hpp"
    --mock-domain-registry-output "${_work_dir}/stale_module_mock_registry__domain_0001_other.hpp"
    --mock-domain-impl-output "${_work_dir}/stale_module_mock_impl__domain_0000_header.hpp"
    --mock-domain-impl-output "${_work_dir}/stale_module_mock_impl__domain_0001_other.hpp"
  RESULT_VARIABLE _stale_module_emit_rc
  OUTPUT_VARIABLE _stale_module_emit_out
  ERROR_VARIABLE _stale_module_emit_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
if(NOT _stale_module_emit_rc EQUAL 1)
  message(FATAL_ERROR
    "emit-mocks should reject named-module mocks omitted from mock_output_domain_modules.\n"
    "--- stdout ---\n${_stale_module_emit_out}\n--- stderr ---\n${_stale_module_emit_err}")
endif()
string(FIND "${_stale_module_emit_out}\n${_stale_module_emit_err}"
  "mock_output_domain_modules does not list it"
  _stale_module_emit_msg_pos)
if(_stale_module_emit_msg_pos EQUAL -1)
  message(FATAL_ERROR
    "emit-mocks emitted the wrong stale-domain diagnostic.\n"
    "--- stdout ---\n${_stale_module_emit_out}\n--- stderr ---\n${_stale_module_emit_err}")
endif()

set(_module_registry "${_work_dir}/module_mock_registry.hpp")
set(_module_impl "${_work_dir}/module_mock_impl.hpp")
set(_module_header_domain_registry "${_work_dir}/module_mock_registry__domain_0000_header.hpp")
set(_module_header_domain_impl "${_work_dir}/module_mock_impl__domain_0000_header.hpp")
set(_module_service_domain_registry "${_work_dir}/module_mock_registry__domain_0001_service.hpp")
set(_module_service_domain_impl "${_work_dir}/module_mock_impl__domain_0001_service.hpp")

execute_process(
  COMMAND "${PROG}"
    emit-mocks
    --mock-manifest-input "${_module_manifest}"
    --mock-registry "${_module_registry}"
    --mock-impl "${_module_impl}"
    --mock-domain-registry-output "${_module_header_domain_registry}"
    --mock-domain-registry-output "${_module_service_domain_registry}"
    --mock-domain-impl-output "${_module_header_domain_impl}"
    --mock-domain-impl-output "${_module_service_domain_impl}"
  RESULT_VARIABLE _module_emit_rc
  OUTPUT_VARIABLE _module_emit_out
  ERROR_VARIABLE _module_emit_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
if(NOT _module_emit_rc EQUAL 0)
  message(FATAL_ERROR
    "emit-mocks named-module manifest emission failed.\n"
    "--- stdout ---\n${_module_emit_out}\n--- stderr ---\n${_module_emit_err}")
endif()
foreach(_generated IN ITEMS
    "${_module_registry}"
    "${_module_impl}"
    "${_module_header_domain_registry}"
    "${_module_header_domain_impl}"
    "${_module_service_domain_registry}"
    "${_module_service_domain_impl}")
  if(NOT EXISTS "${_generated}")
    message(FATAL_ERROR "Expected named-module emit-mocks output '${_generated}'")
  endif()
endforeach()

file(READ "${_module_service_domain_registry}" _module_service_registry_text)
foreach(_token IN ITEMS "mock_manifest_split_module::Service" "MockAccess")
  string(FIND "${_module_service_registry_text}" "${_token}" _module_service_registry_token_pos)
  if(_module_service_registry_token_pos EQUAL -1)
    message(FATAL_ERROR "Expected named-module registry token '${_token}'.\n${_module_service_registry_text}")
  endif()
endforeach()

file(READ "${_module_service_domain_impl}" _module_service_impl_text)
foreach(_token IN ITEMS "mock_manifest_split_module::Service" "dispatch_with_fallback")
  string(FIND "${_module_service_impl_text}" "${_token}" _module_service_impl_token_pos)
  if(_module_service_impl_token_pos EQUAL -1)
    message(FATAL_ERROR "Expected named-module impl token '${_token}'.\n${_module_service_impl_text}")
  endif()
endforeach()

message(STATUS "Mock manifest split discovery/emission regression passed")
