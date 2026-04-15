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
file(MAKE_DIRECTORY "${_module_smoke_dir}")
file(WRITE "${_module_smoke_source}" [=[
export module gentest.cli.validation;

[[using gentest: test("cli/module_wrapper_output_required")]]
void module_wrapper_output_required_case() {}
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
list(APPEND _clang_args "${CODEGEN_STD}" ${_public_include_args})

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
  "tu header output count mismatch"
  1
  "gentest_codegen: expected 1 --tu-header-output value(s) for 1 input source(s), got 2"
  "${PROG}"
  --tu-out-dir "${BUILD_ROOT}/unused-tu-headers"
  --tu-header-output "${BUILD_ROOT}/a.gentest.h"
  --tu-header-output "${BUILD_ROOT}/b.gentest.h"
  ${_common_args})

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
  ${_clang_args})

_gentest_expect_result(
  "mock domain outputs require base outputs"
  1
  "gentest_codegen: explicit mock domain outputs require both --mock-registry and --mock-impl"
  "${PROG}"
  --mock-domain-registry-output "${BUILD_ROOT}/a_mock_registry__domain_0000_header.hpp"
  --mock-domain-impl-output "${BUILD_ROOT}/a_mock_impl__domain_0000_header.hpp"
  ${_common_args})

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
