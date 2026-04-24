# Validates summary/JUnit/exit-code coherence for core outcomes.
# Required defines:
#   PROG_UNIT, PROG_OUTCOMES, PROG_INFRA_TEST, PROG_INFRA_MEASURED, OUT_DIR

if(NOT DEFINED PROG_UNIT)
  message(FATAL_ERROR "CheckOutcomeConsistencyMatrix.cmake: PROG_UNIT not set")
endif()
if(NOT DEFINED PROG_OUTCOMES)
  message(FATAL_ERROR "CheckOutcomeConsistencyMatrix.cmake: PROG_OUTCOMES not set")
endif()
if(NOT DEFINED PROG_INFRA_TEST)
  message(FATAL_ERROR "CheckOutcomeConsistencyMatrix.cmake: PROG_INFRA_TEST not set")
endif()
if(NOT DEFINED PROG_INFRA_MEASURED)
  message(FATAL_ERROR "CheckOutcomeConsistencyMatrix.cmake: PROG_INFRA_MEASURED not set")
endif()
if(NOT DEFINED OUT_DIR)
  message(FATAL_ERROR "CheckOutcomeConsistencyMatrix.cmake: OUT_DIR not set")
endif()

set(_emu)
if(DEFINED EMU)
  if(EMU MATCHES ";")
    set(_emu ${EMU})
  else()
    separate_arguments(_emu NATIVE_COMMAND "${EMU}")
  endif()
endif()

file(MAKE_DIRECTORY "${OUT_DIR}")

function(run_matrix_case _label _prog _expect_rc _expect_summary _expect_junit)
  set(_junit "${OUT_DIR}/${_label}.xml")
  # Guard against stale files masking missing JUnit output from the current run.
  file(REMOVE "${_junit}")

  execute_process(
    COMMAND ${_emu} "${_prog}" ${ARGN} --junit=${_junit}
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)

  set(_all "${_out}\n${_err}")

  if(NOT _rc EQUAL _expect_rc)
    message(FATAL_ERROR "${_label}: expected exit code ${_expect_rc}, got ${_rc}. Output:\n${_all}")
  endif()

  string(FIND "${_all}" "${_expect_summary}" _summary_pos)
  if(_summary_pos EQUAL -1)
    message(FATAL_ERROR "${_label}: expected summary substring not found: '${_expect_summary}'. Output:\n${_all}")
  endif()

  if(NOT EXISTS "${_junit}")
    message(FATAL_ERROR "${_label}: expected JUnit file not found: ${_junit}")
  endif()

  file(READ "${_junit}" _junit_content)
  string(FIND "${_junit_content}" "${_expect_junit}" _junit_pos)
  if(_junit_pos EQUAL -1)
    message(FATAL_ERROR "${_label}: expected JUnit substring not found: '${_expect_junit}'. File:\n${_junit_content}")
  endif()

  if(_label STREQUAL "infra_skip_test")
    string(FIND "${_junit_content}" "<skipped" _skip_pos)
    if(_skip_pos EQUAL -1)
      message(FATAL_ERROR "${_label}: shared fixture infrastructure failures must mark dependent cases as blocked skips. File:\n${_junit_content}")
    endif()

    foreach(_expected_issue IN ITEMS
        "shared fixture unavailable for 'regressions::shared_setup_skip::NullGlobalFx': fixture allocation returned null"
        "shared fixture unavailable for 'regressions::shared_setup_skip::NullSuiteFx': fixture allocation returned null")
      string(FIND "${_junit_content}" "${_expected_issue}" _issue_pos)
      if(_issue_pos EQUAL -1)
        message(FATAL_ERROR "${_label}: expected failure issue not found: '${_expected_issue}'. File:\n${_junit_content}")
      endif()
    endforeach()

    string(FIND "${_junit_content}" "<failure>" _failure_pos)
    if(NOT _failure_pos EQUAL -1)
      message(FATAL_ERROR "${_label}: shared fixture infrastructure failures must not emit per-case failure elements. File:\n${_junit_content}")
    endif()
  endif()
endfunction()

run_matrix_case(
  pass
  "${PROG_UNIT}"
  0
  "Summary: passed 1/1; failed 0; skipped 0; blocked 0; xfail 0; xpass 0."
  "tests=\"1\" failures=\"0\" skipped=\"0\" errors=\"0\""
  --run=unit/arithmetic/sum
  --kind=test)

run_matrix_case(
  fail
  "${PROG_OUTCOMES}"
  1
  "Summary: passed 0/1; failed 1; skipped 0; blocked 0; xfail 0; xpass 0."
  "tests=\"1\" failures=\"1\" skipped=\"0\" errors=\"0\""
  --run=outcomes/skip_after_failure_is_fail
  --kind=test)

run_matrix_case(
  skip
  "${PROG_OUTCOMES}"
  0
  "Summary: passed 0/1; failed 0; skipped 1; blocked 0; xfail 0; xpass 0."
  "tests=\"1\" failures=\"0\" skipped=\"1\" errors=\"0\""
  --run=outcomes/runtime_skip_simple
  --kind=test)

run_matrix_case(
  xfail
  "${PROG_OUTCOMES}"
  0
  "Summary: passed 0/1; failed 0; skipped 1; blocked 0; xfail 1; xpass 0."
  "tests=\"1\" failures=\"0\" skipped=\"1\" errors=\"0\""
  --run=outcomes/xfail_expect_fail
  --kind=test)

run_matrix_case(
  xpass
  "${PROG_OUTCOMES}"
  1
  "Summary: passed 0/1; failed 1; skipped 0; blocked 0; xfail 0; xpass 1."
  "tests=\"1\" failures=\"1\" skipped=\"0\" errors=\"0\""
  --run=outcomes/xfail_xpass
  --kind=test)

run_matrix_case(
  infra_skip_test
  "${PROG_INFRA_TEST}"
  1
  "Summary: passed 0/2; failed 2; skipped 0; blocked 2; xfail 0; xpass 0."
  "tests=\"2\" failures=\"0\" skipped=\"2\" errors=\"2\""
  --kind=test)

run_matrix_case(
  infra_skip_measured
  "${PROG_INFRA_MEASURED}"
  1
  "Summary: passed 0/1; failed 1; skipped 0; blocked 0; xfail 0; xpass 0."
  "tests=\"1\" failures=\"1\" skipped=\"0\" errors=\"0\""
  --run=regressions/member_shared_setup_skip_measured/bench_member
  --kind=bench)
