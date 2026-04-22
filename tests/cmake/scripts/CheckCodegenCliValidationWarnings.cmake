if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenCliValidationWarnings.cmake: PROG not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenCliValidationWarnings.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenCliValidationWarnings.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED CODEGEN_STD OR "${CODEGEN_STD}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenCliValidationWarnings.cmake: CODEGEN_STD not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

set(_compdb_root "${BUILD_ROOT}")
if(DEFINED COMPDB_ROOT AND NOT "${COMPDB_ROOT}" STREQUAL "")
  set(_compdb_root "${COMPDB_ROOT}")
endif()

set(_smoke_source "${SOURCE_DIR}/tests/smoke/codegen_axis_generators.cpp")
if(NOT EXISTS "${_smoke_source}")
  message(FATAL_ERROR "CheckCodegenCliValidationWarnings.cmake: missing smoke source '${_smoke_source}'")
endif()
set(_module_smoke_dir "${BUILD_ROOT}/cli_validation_module_fixture")
set(_module_smoke_source "${_module_smoke_dir}/cases.cppm")
set(_module_partition_source "${_module_smoke_dir}/partition.cppm")
set(_module_pmf_source "${_module_smoke_dir}/pmf.cppm")
file(MAKE_DIRECTORY "${_module_smoke_dir}")
file(WRITE "${_module_smoke_source}" [=[
export module gentest.cli.validation;

[[using gentest: test("cli/module_wrapper_output_required")]]
void module_wrapper_output_required_case() {}
]=])
file(WRITE "${_module_partition_source}" [=[
export module gentest.cli.validation:partition;

[[using gentest: test("cli/module_registration_partition_rejected")]]
void module_registration_partition_rejected_case() {}
]=])
file(WRITE "${_module_pmf_source}" [=[
export module gentest.cli.validation.pmf;

[[using gentest: test("cli/module_registration_pmf_rejected")]]
void module_registration_pmf_rejected_case() {}

module :private;
]=])

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

set(_module_clang_args)
if(DEFINED TARGET_ARG AND NOT "${TARGET_ARG}" STREQUAL "")
  list(APPEND _module_clang_args "${TARGET_ARG}")
endif()
set(_module_compiler "clang++")
set(_compdb_cache_file "${_compdb_root}/CMakeCache.txt")
gentest_read_cache_value("${_compdb_cache_file}" "CMAKE_CXX_COMPILER" _module_compiler_found _module_compiler_from_cache)
if(_module_compiler_found AND NOT "${_module_compiler_from_cache}" STREQUAL "")
  set(_module_compiler "${_module_compiler_from_cache}")
endif()
gentest_normalize_std_flag_for_compiler(_module_codegen_std "${_module_compiler}" "${CODEGEN_STD}")
list(APPEND _module_clang_args "${_module_codegen_std}" ${_public_include_args})

set(_common_args
  --check
  --compdb "${_compdb_root}"
  "${_smoke_source}"
  --
  ${_clang_args})

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

  string(FIND "${_combined}" "${expected_substring}" _expected_pos)
  if(_expected_pos EQUAL -1)
    message(FATAL_ERROR
      "${label}: expected to find '${expected_substring}'.\n"
      "--- stdout ---\n${_out}\n--- stderr ---\n${_err}")
  endif()
endfunction()

_gentest_expect_result(
  "tu header output requires tu out dir"
  1
  "gentest_codegen: --tu-header-output requires --tu-out-dir"
  "${PROG}"
  --tu-header-output "${BUILD_ROOT}/unused.gentest.h"
  ${_common_args})

_gentest_expect_result(
  "output option removed"
  1
  "gentest_codegen: --output legacy manifest/single-TU mode was removed in gentest 2.0.0"
  "${PROG}"
  --output "${BUILD_ROOT}/manifest.cpp"
  --tu-out-dir "${BUILD_ROOT}/tu-mode"
  ${_common_args})

_gentest_expect_result(
  "template option removed"
  1
  "gentest_codegen: --template was removed with legacy manifest/single-TU mode in gentest 2.0.0"
  "${PROG}"
  --template "${BUILD_ROOT}/legacy-template.tpl"
  --tu-out-dir "${BUILD_ROOT}/template-removed"
  ${_common_args})

_gentest_expect_result(
  "tu header output count mismatch"
  1
  "gentest_codegen: expected 1 --tu-header-output value(s) for 1 input source(s), got 2"
  "${PROG}"
  --tu-out-dir "${BUILD_ROOT}/unused-tu-headers"
  --tu-header-output "${BUILD_ROOT}/a.gentest.h"
  --tu-header-output "${BUILD_ROOT}/b.gentest.h"
  ${_common_args})

_gentest_expect_result(
  "textual wrapper output requires tu out dir"
  1
  "gentest_codegen: --textual-wrapper-output requires --tu-out-dir"
  "${PROG}"
  --textual-wrapper-output "${BUILD_ROOT}/unused.gentest.cpp"
  ${_common_args})

_gentest_expect_result(
  "textual wrapper output count mismatch"
  1
  "gentest_codegen: expected 1 --textual-wrapper-output value(s) for 1 input source(s), got 2"
  "${PROG}"
  --tu-out-dir "${BUILD_ROOT}/unused-textual-wrappers"
  --textual-wrapper-output "${BUILD_ROOT}/a.gentest.cpp"
  --textual-wrapper-output "${BUILD_ROOT}/b.gentest.cpp"
  ${_common_args})

_gentest_expect_result(
  "textual wrapper output rejects named modules"
  1
  "gentest_codegen: --textual-wrapper-output does not support named module source '${_module_smoke_source}'"
  "${PROG}"
  --check
  --compdb "${_compdb_root}"
  --tu-out-dir "${BUILD_ROOT}/textual-wrapper-module"
  --textual-wrapper-output "${BUILD_ROOT}/textual-wrapper-module/cases.gentest.cpp"
  "${_module_smoke_source}"
  --
  ${_module_clang_args})

_gentest_expect_result(
  "module wrapper output requires tu out dir"
  1
  "gentest_codegen: --module-wrapper-output requires --tu-out-dir"
  "${PROG}"
  --module-wrapper-output "${BUILD_ROOT}/unused.module.gentest.cpp"
  ${_common_args})

_gentest_expect_result(
  "module wrapper output count mismatch"
  1
  "gentest_codegen: expected 1 --module-wrapper-output value(s) for 1 input source(s), got 2"
  "${PROG}"
  --tu-out-dir "${BUILD_ROOT}/unused-module-wrappers"
  --module-wrapper-output "${BUILD_ROOT}/a.module.gentest.cpp"
  --module-wrapper-output "${BUILD_ROOT}/b.module.gentest.cpp"
  ${_common_args})

_gentest_expect_result(
  "module source requires explicit wrapper output"
  1
  "gentest_codegen: named module source '${_module_smoke_source}' requires an explicit --module-wrapper-output path in TU mode"
  "${PROG}"
  --check
  --compdb "${_compdb_root}"
  --tu-out-dir "${BUILD_ROOT}/missing-module-wrapper"
  "${_module_smoke_source}"
  --
  ${_module_clang_args})

_gentest_expect_result(
  "module registration output requires tu out dir"
  1
  "gentest_codegen: --module-registration-output requires --tu-out-dir"
  "${PROG}"
  --module-registration-output "${BUILD_ROOT}/unused.registration.gentest.cpp"
  ${_common_args})

_gentest_expect_result(
  "module registration output count mismatch"
  1
  "gentest_codegen: expected 1 --module-registration-output value(s) for 1 input source(s), got 2"
  "${PROG}"
  --tu-out-dir "${BUILD_ROOT}/unused-module-registrations"
  --module-registration-output "${BUILD_ROOT}/a.registration.gentest.cpp"
  --module-registration-output "${BUILD_ROOT}/b.registration.gentest.cpp"
  ${_common_args})

_gentest_expect_result(
  "module registration output cannot combine with wrapper output"
  1
  "gentest_codegen: --module-registration-output cannot be combined with --module-wrapper-output"
  "${PROG}"
  --tu-out-dir "${BUILD_ROOT}/mixed-module-outputs"
  --module-wrapper-output "${BUILD_ROOT}/a.module.gentest.cpp"
  --module-registration-output "${BUILD_ROOT}/a.registration.gentest.cpp"
  ${_common_args})

_gentest_expect_result(
  "textual wrapper output cannot combine with module outputs"
  1
  "gentest_codegen: --textual-wrapper-output cannot be combined with module wrapper/registration outputs"
  "${PROG}"
  --tu-out-dir "${BUILD_ROOT}/mixed-textual-module-outputs"
  --textual-wrapper-output "${BUILD_ROOT}/a.gentest.cpp"
  --module-wrapper-output "${BUILD_ROOT}/a.module.gentest.cpp"
  ${_common_args})

_gentest_expect_result(
  "compile context id count mismatch"
  1
  "gentest_codegen: expected 1 --compile-context-id value(s) for 1 input source(s), got 2"
  "${PROG}"
  --compile-context-id a
  --compile-context-id b
  ${_common_args})

_gentest_expect_result(
  "artifact owner source requires tu out dir"
  1
  "gentest_codegen: --artifact-owner-source requires --tu-out-dir"
  "${PROG}"
  --artifact-manifest "${BUILD_ROOT}/unused.artifact_manifest.json"
  --artifact-owner-source "${_smoke_source}"
  ${_common_args})

set(_empty_textual_manifest "${BUILD_ROOT}/empty-textual-artifact-manifest-dir/manifest.json")
execute_process(
  COMMAND "${PROG}"
    --compdb "${_compdb_root}"
    --tu-out-dir "${BUILD_ROOT}/empty-textual-artifact-owner"
    --artifact-manifest "${_empty_textual_manifest}"
    "${_smoke_source}"
    --
    ${_clang_args}
  RESULT_VARIABLE _empty_textual_manifest_rc
  OUTPUT_VARIABLE _empty_textual_manifest_out
  ERROR_VARIABLE _empty_textual_manifest_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
if(NOT _empty_textual_manifest_rc EQUAL 0)
  message(FATAL_ERROR
    "textual artifact manifest without owner source should preserve the old empty-manifest behavior.\n"
    "--- stdout ---\n${_empty_textual_manifest_out}\n--- stderr ---\n${_empty_textual_manifest_err}")
endif()
if(NOT EXISTS "${_empty_textual_manifest}")
  message(FATAL_ERROR "Expected empty textual artifact manifest '${_empty_textual_manifest}'")
endif()
file(READ "${_empty_textual_manifest}" _empty_textual_manifest_json)
foreach(_expected_empty_manifest_token IN ITEMS "\"sources\": []" "\"artifacts\": []")
  string(FIND "${_empty_textual_manifest_json}" "${_expected_empty_manifest_token}" _empty_manifest_pos)
  if(_empty_manifest_pos EQUAL -1)
    message(FATAL_ERROR
      "Expected empty textual manifest token '${_expected_empty_manifest_token}'.\n"
      "${_empty_textual_manifest_json}")
  endif()
endforeach()

_gentest_expect_result(
  "artifact owner source count mismatch"
  1
  "gentest_codegen: expected 1 --artifact-owner-source value(s) for 1 input source(s), got 2"
  "${PROG}"
  --tu-out-dir "${BUILD_ROOT}/artifact-owner-count"
  --artifact-manifest "${BUILD_ROOT}/artifact-owner-count.json"
  --artifact-owner-source "${_smoke_source}"
  --artifact-owner-source "${_module_smoke_source}"
  ${_common_args})

_gentest_expect_result(
  "textual artifact manifest rejects named module wrapper source"
  1
  "gentest_codegen: textual artifact manifests cannot describe named module source '${_module_smoke_source}'; use --module-registration-output"
  "${PROG}"
  --check
  --compdb "${_compdb_root}"
  --tu-out-dir "${BUILD_ROOT}/artifact-owner-module-wrapper"
  --module-wrapper-output "${BUILD_ROOT}/artifact-owner-module-wrapper/a.module.gentest.cpp"
  --artifact-manifest "${BUILD_ROOT}/artifact-owner-module-wrapper.json"
  --artifact-owner-source "${_module_smoke_source}"
  "${_module_smoke_source}"
  --
  ${_module_clang_args})

_gentest_expect_result(
  "module registration rejects partitions"
  1
  "gentest_codegen: module registration input '${_module_partition_source}' declares module partition 'gentest.cli.validation:partition'"
  "${PROG}"
  --check
  --compdb "${_compdb_root}"
  --tu-out-dir "${BUILD_ROOT}/partition-module-registration"
  --module-registration-output "${BUILD_ROOT}/partition-module-registration/partition.registration.gentest.cpp"
  "${_module_partition_source}"
  --
  ${_module_clang_args})

_gentest_expect_result(
  "module registration rejects private module fragments"
  1
  "gentest_codegen: module registration input '${_module_pmf_source}' contains a private module fragment"
  "${PROG}"
  --check
  --compdb "${_compdb_root}"
  --tu-out-dir "${BUILD_ROOT}/pmf-module-registration"
  --module-registration-output "${BUILD_ROOT}/pmf-module-registration/pmf.registration.gentest.cpp"
  "${_module_pmf_source}"
  --
  ${_module_clang_args})

_gentest_expect_result(
  "mock domain outputs require base outputs"
  1
  "gentest_codegen: explicit mock domain outputs require both --mock-registry and --mock-impl"
  "${PROG}"
  --mock-domain-registry-output "${BUILD_ROOT}/a_mock_registry__domain_0000_header.hpp"
  --mock-domain-impl-output "${BUILD_ROOT}/a_mock_impl__domain_0000_header.hpp"
  ${_common_args})

_gentest_expect_result(
  "mock public header requires base outputs"
  1
  "gentest_codegen: --mock-public-header requires --mock-registry and --mock-impl"
  "${PROG}"
  --mock-public-header "${BUILD_ROOT}/mocks.hpp"
  ${_common_args})

_gentest_expect_result(
  "mock public header source count"
  1
  "gentest_codegen: --mock-public-header expects exactly 1 input source, got 2"
  "${PROG}"
  --mock-registry "${BUILD_ROOT}/mock-public-count_registry.hpp"
  --mock-impl "${BUILD_ROOT}/mock-public-count_impl.hpp"
  --mock-public-header "${BUILD_ROOT}/mock-public-count.hpp"
  --compdb "${_compdb_root}"
  "${_smoke_source}"
  "${_module_smoke_source}"
  --
  ${_clang_args})

_gentest_expect_result(
  "mock outputs require explicit domain outputs"
  1
  "gentest_codegen: mock outputs require explicit --mock-domain-registry-output/--mock-domain-impl-output paths"
  "${PROG}"
  --mock-registry "${BUILD_ROOT}/mock_registry.hpp"
  --mock-impl "${BUILD_ROOT}/mock_impl.hpp"
  ${_common_args})

_gentest_expect_result(
  "mock domain output count mismatch"
  1
  "gentest_codegen: expected 1 --mock-domain-registry-output/--mock-domain-impl-output value(s) for discovered mock output domains, got 2 and 2"
  "${PROG}"
  --mock-registry "${BUILD_ROOT}/mock_registry.hpp"
  --mock-impl "${BUILD_ROOT}/mock_impl.hpp"
  --mock-domain-registry-output "${BUILD_ROOT}/mock_registry__domain_0000_header.hpp"
  --mock-domain-registry-output "${BUILD_ROOT}/mock_registry__domain_0001_extra.hpp"
  --mock-domain-impl-output "${BUILD_ROOT}/mock_impl__domain_0000_header.hpp"
  --mock-domain-impl-output "${BUILD_ROOT}/mock_impl__domain_0001_extra.hpp"
  ${_common_args})

_gentest_expect_result(
  "invalid jobs env warning"
  0
  "gentest_codegen: warning: ignoring invalid GENTEST_CODEGEN_JOBS='bogus'"
  "${CMAKE_COMMAND}" -E env
  GENTEST_CODEGEN_JOBS=bogus
  "${PROG}"
  ${_common_args})

_gentest_expect_result(
  "invalid scan deps cli warning"
  0
  "gentest_codegen: warning: ignoring invalid --scan-deps-mode='bogus'; using AUTO"
  "${PROG}"
  --scan-deps-mode=bogus
  ${_common_args})

_gentest_expect_result(
  "invalid scan deps env warning"
  0
  "gentest_codegen: warning: ignoring invalid GENTEST_CODEGEN_SCAN_DEPS_MODE='bogus'"
  "${CMAKE_COMMAND}" -E env
  GENTEST_CODEGEN_SCAN_DEPS_MODE=bogus
  "${PROG}"
  ${_common_args})

_gentest_expect_result(
  "missing compdb load failure"
  1
  "gentest_codegen: failed to load compilation database at '${_compdb_root}/missing-compdb'"
  "${PROG}"
  --check
  --compdb "${_compdb_root}/missing-compdb"
  "${_smoke_source}"
  -- ${_clang_args})
