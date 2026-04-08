if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenCliValidationWarnings.cmake: PROG not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenCliValidationWarnings.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenCliValidationWarnings.cmake: SOURCE_DIR not set")
endif()

set(_compdb_root "${BUILD_ROOT}")
if(DEFINED COMPDB_ROOT AND NOT "${COMPDB_ROOT}" STREQUAL "")
  set(_compdb_root "${COMPDB_ROOT}")
endif()

set(_smoke_source "${SOURCE_DIR}/tests/smoke/codegen_axis_generators.cpp")
if(NOT EXISTS "${_smoke_source}")
  message(FATAL_ERROR "CheckCodegenCliValidationWarnings.cmake: missing smoke source '${_smoke_source}'")
endif()

set(_common_args
  --check
  --compdb "${_compdb_root}"
  "${_smoke_source}"
  --
  -std=c++20
  "-I${SOURCE_DIR}/include"
  "-I${SOURCE_DIR}/tests")

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
  --
  -std=c++20
  "-I${SOURCE_DIR}/include"
  "-I${SOURCE_DIR}/tests")
