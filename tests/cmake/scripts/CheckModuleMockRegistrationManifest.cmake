if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleMockRegistrationManifest.cmake: PROG not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleMockRegistrationManifest.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleMockRegistrationManifest.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED CODEGEN_STD OR "${CODEGEN_STD}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleMockRegistrationManifest.cmake: CODEGEN_STD not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

set(_work_dir "${BUILD_ROOT}/module_mock_registration_manifest")
set(_generated_dir "${_work_dir}/generated")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}" "${_generated_dir}")

set(_module_source "${_work_dir}/module_service.cppm")
file(WRITE "${_module_source}" [=[
export module gentest.module_mock_registration_manifest;

export namespace module_mock_registration_manifest {

struct Service {
    virtual ~Service() = default;
    virtual int value(int input) const = 0;
};

[[using gentest: test("module_mock_registration_manifest/direct")]]
void direct_case() {}

} // namespace module_mock_registration_manifest
]=])

set(_manifest "${_work_dir}/service.mock_manifest.json")
file(TO_CMAKE_PATH "${_module_source}" _module_source_json)
file(WRITE "${_manifest}" [=[
{
  "schema": "gentest.mock_manifest.v1",
  "mock_output_domain_modules": [
    "gentest.module_mock_registration_manifest"
  ],
  "mocks": [
    {
      "qualified_name": "module_mock_registration_manifest::Service",
      "display_name": "module_mock_registration_manifest::Service",
      "definition_file": "]=] "${_module_source_json}" [=[",
      "definition_kind": "named_module",
      "use_files": [
        "module_mock_registration_manifest_consumer.cppm"
      ],
      "definition_module_name": "gentest.module_mock_registration_manifest",
      "attachment_namespace_chain": [],
      "derive_for_virtual": true,
      "has_accessible_default_ctor": false,
      "has_virtual_destructor": true,
      "constructors": [],
      "methods": [
        {
          "qualified_name": "module_mock_registration_manifest::Service::value",
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
list(APPEND _clang_args "${_codegen_std}" ${_public_include_args})

function(_gentest_expect_result label expected_rc expected_substring)
  execute_process(
    COMMAND ${ARGN}
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)

  set(_combined "${_out}\n${_err}")
  if(NOT _rc EQUAL ${expected_rc})
    message(FATAL_ERROR
      "${label}: expected exit code ${expected_rc}, got ${_rc}.\n"
      "--- stdout ---\n${_out}\n--- stderr ---\n${_err}")
  endif()

  if(NOT "${expected_substring}" STREQUAL "")
    string(FIND "${_combined}" "${expected_substring}" _expected_pos)
    if(_expected_pos EQUAL -1)
      message(FATAL_ERROR
        "${label}: expected to find '${expected_substring}'.\n"
        "--- stdout ---\n${_out}\n--- stderr ---\n${_err}")
    endif()
  endif()
endfunction()

set(_header "${_generated_dir}/tu_0000_module_service.gentest.h")
set(_registration "${_generated_dir}/tu_0000_module_service.registration.gentest.cpp")
set(_depfile "${_generated_dir}/tu_0000_module_service.d")

_gentest_expect_result(
  "registration manifest mode emits same-module mock attachment"
  0
  ""
  "${PROG}"
  --tu-out-dir "${_generated_dir}"
  --tu-header-output "${_header}"
  --module-registration-output "${_registration}"
  --mock-registration-manifest "${_manifest}"
  --depfile "${_depfile}"
  "${_module_source}"
  --
  ${_clang_args})

foreach(_generated IN ITEMS "${_header}" "${_registration}" "${_depfile}")
  if(NOT EXISTS "${_generated}")
    message(FATAL_ERROR "Expected module mock registration manifest output '${_generated}'")
  endif()
endforeach()

file(READ "${_registration}" _registration_text)
foreach(_token IN ITEMS
    "module gentest.module_mock_registration_manifest;"
    "gentest_codegen: injected mock attachment"
    "mock<::module_mock_registration_manifest::Service>"
    "MockAccess<mock<::module_mock_registration_manifest::Service>>"
    "#include \"tu_0000_module_service.gentest.h\"")
  string(FIND "${_registration_text}" "${_token}" _token_pos)
  if(_token_pos EQUAL -1)
    message(FATAL_ERROR "Expected registration output token '${_token}'.\n${_registration_text}")
  endif()
endforeach()

foreach(_forbidden IN ITEMS
    "#include \"${_module_source_json}\""
    "import gentest.module_mock_registration_manifest")
  string(FIND "${_registration_text}" "${_forbidden}" _forbidden_pos)
  if(NOT _forbidden_pos EQUAL -1)
    message(FATAL_ERROR "Registration output unexpectedly contains '${_forbidden}'.\n${_registration_text}")
  endif()
endforeach()

string(FIND "${_registration_text}" "gentest_codegen: injected mock attachment" _attachment_pos)
string(FIND "${_registration_text}" "#include \"tu_0000_module_service.gentest.h\"" _header_include_pos)
if(_attachment_pos EQUAL -1 OR _header_include_pos EQUAL -1 OR _attachment_pos GREATER_EQUAL _header_include_pos)
  message(FATAL_ERROR
    "Expected mock attachment to be emitted before the generated registration header include.\n${_registration_text}")
endif()

file(READ "${_depfile}" _depfile_text)
string(FIND "${_depfile_text}" "service.mock_manifest.json" _depfile_manifest_pos)
if(_depfile_manifest_pos EQUAL -1)
  message(FATAL_ERROR "Expected depfile to include the mock registration manifest dependency.\n${_depfile_text}")
endif()

_gentest_expect_result(
  "registration manifest validates during check-only mode"
  0
  ""
  "${PROG}"
  --check
  --tu-out-dir "${_generated_dir}/check"
  --tu-header-output "${_generated_dir}/check/tu_0000_module_service.gentest.h"
  --module-registration-output "${_generated_dir}/check/tu_0000_module_service.registration.gentest.cpp"
  --mock-registration-manifest "${_manifest}"
  "${_module_source}"
  --
  ${_clang_args})

set(_bad_schema_manifest "${_work_dir}/service.bad_schema.mock_manifest.json")
file(READ "${_manifest}" _manifest_json)
string(JSON _bad_schema_manifest_json SET "${_manifest_json}" schema "\"gentest.mock_manifest.v0\"")
file(WRITE "${_bad_schema_manifest}" "${_bad_schema_manifest_json}")
_gentest_expect_result(
  "registration manifest rejects bad schema during check-only mode"
  1
  "unsupported mock manifest schema"
  "${PROG}"
  --check
  --tu-out-dir "${_generated_dir}/bad-schema"
  --tu-header-output "${_generated_dir}/bad-schema/tu_0000_module_service.gentest.h"
  --module-registration-output "${_generated_dir}/bad-schema/tu_0000_module_service.registration.gentest.cpp"
  --mock-registration-manifest "${_bad_schema_manifest}"
  "${_module_source}"
  --
  ${_clang_args})

set(_stale_module_manifest "${_work_dir}/service.stale_module.mock_manifest.json")
string(REPLACE
  "\"definition_module_name\": \"gentest.module_mock_registration_manifest\""
  "\"definition_module_name\": \"gentest.module_mock_registration_manifest.stale\""
  _stale_module_manifest_json
  "${_manifest_json}")
string(REPLACE
  "\"gentest.module_mock_registration_manifest\""
  "\"gentest.module_mock_registration_manifest.stale\""
  _stale_module_manifest_json
  "${_stale_module_manifest_json}")
file(WRITE "${_stale_module_manifest}" "${_stale_module_manifest_json}")
_gentest_expect_result(
  "registration manifest rejects stale module during check-only mode"
  1
  "no module registration input provides it"
  "${PROG}"
  --check
  --tu-out-dir "${_generated_dir}/stale-module"
  --tu-header-output "${_generated_dir}/stale-module/tu_0000_module_service.gentest.h"
  --module-registration-output "${_generated_dir}/stale-module/tu_0000_module_service.registration.gentest.cpp"
  --mock-registration-manifest "${_stale_module_manifest}"
  "${_module_source}"
  --
  ${_clang_args})

set(_header_like_manifest "${_work_dir}/service.header_like.mock_manifest.json")
string(REPLACE
  "\"definition_kind\": \"named_module\""
  "\"definition_kind\": \"header_like\""
  _header_like_manifest_json
  "${_manifest_json}")
file(WRITE "${_header_like_manifest}" "${_header_like_manifest_json}")
_gentest_expect_result(
  "registration manifest rejects header-like mocks"
  1
  "--mock-registration-manifest does not support header-like mock"
  "${PROG}"
  --tu-out-dir "${_generated_dir}/header-like"
  --tu-header-output "${_generated_dir}/header-like/tu_0000_module_service.gentest.h"
  --module-registration-output "${_generated_dir}/header-like/tu_0000_module_service.registration.gentest.cpp"
  --mock-registration-manifest "${_header_like_manifest}"
  "${_module_source}"
  --
  ${_clang_args})

_gentest_expect_result(
  "registration manifest rejects discover-mocks"
  1
  "--mock-registration-manifest cannot be combined with --discover-mocks"
  "${PROG}"
  --discover-mocks
  --tu-out-dir "${_generated_dir}/invalid-discover"
  --tu-header-output "${_generated_dir}/invalid-discover/tu_0000_module_service.gentest.h"
  --module-registration-output "${_generated_dir}/invalid-discover/tu_0000_module_service.registration.gentest.cpp"
  --mock-registration-manifest "${_manifest}"
  "${_module_source}"
  --
  ${_clang_args})

_gentest_expect_result(
  "registration manifest requires module registration output"
  1
  "--mock-registration-manifest requires --tu-out-dir and --module-registration-output"
  "${PROG}"
  --tu-out-dir "${_generated_dir}/missing-registration"
  --tu-header-output "${_generated_dir}/missing-registration/tu_0000_module_service.gentest.h"
  --mock-registration-manifest "${_manifest}"
  "${_module_source}"
  --
  ${_clang_args})

_gentest_expect_result(
  "registration manifest rejects emit-mocks phase"
  1
  "emit-mocks cannot be combined with --mock-registration-manifest"
  "${PROG}"
  emit-mocks
  --mock-manifest-input "${_manifest}"
  --mock-registration-manifest "${_manifest}"
  --mock-registry "${_generated_dir}/unused_registry.hpp"
  --mock-impl "${_generated_dir}/unused_impl.hpp")

message(STATUS "Module mock registration manifest regression passed")
