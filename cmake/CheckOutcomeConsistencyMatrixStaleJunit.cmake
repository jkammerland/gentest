# Proves CheckOutcomeConsistencyMatrix does not pass on stale JUnit XML.
# This script seeds stale XML files, invokes the matrix script with a fake runner
# that never writes JUnit output, and expects the matrix script to fail.

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckOutcomeConsistencyMatrixStaleJunit.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT)
  message(FATAL_ERROR "CheckOutcomeConsistencyMatrixStaleJunit.cmake: BUILD_ROOT not set")
endif()

set(_matrix_script "${SOURCE_DIR}/cmake/CheckOutcomeConsistencyMatrix.cmake")
if(NOT EXISTS "${_matrix_script}")
  message(FATAL_ERROR "Matrix script not found: ${_matrix_script}")
endif()

set(_work_dir "${BUILD_ROOT}/outcome_matrix_stale_junit")
set(_out_dir "${_work_dir}/out")
set(_fake_runner "${_work_dir}/fake_runner.cmake")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_out_dir}")

file(
  WRITE "${_fake_runner}"
  [=[
set(_prog "")
set(_run "")

math(EXPR _last "${CMAKE_ARGC}-1")
foreach(i RANGE 0 ${_last})
  set(_arg "${CMAKE_ARGV${i}}")
  if(i EQUAL 3)
    set(_prog "${_arg}")
  endif()
  if(_arg MATCHES "^--run=")
    string(REGEX REPLACE "^--run=" "" _run "${_arg}")
  endif()
endforeach()

if(_run STREQUAL "unit/arithmetic/sum")
  message("Summary: passed 1/1; failed 0; skipped 0; xfail 0; xpass 0.")
  return()
endif()

if(_run STREQUAL "outcomes/skip_after_failure_is_fail")
  message("Summary: passed 0/1; failed 1; skipped 0; xfail 0; xpass 0.")
  message(FATAL_ERROR "intentional nonzero")
endif()

if(_run STREQUAL "outcomes/runtime_skip_simple")
  message("Summary: passed 0/1; failed 0; skipped 1; xfail 0; xpass 0.")
  return()
endif()

if(_run STREQUAL "outcomes/xfail_expect_fail")
  message("Summary: passed 0/1; failed 0; skipped 1; xfail 1; xpass 0.")
  return()
endif()

if(_run STREQUAL "outcomes/xfail_xpass")
  message("Summary: passed 0/1; failed 1; skipped 0; xfail 0; xpass 1.")
  message(FATAL_ERROR "intentional nonzero")
endif()

if(_prog STREQUAL "infra-test-prog")
  message("Summary: passed 0/2; failed 4; skipped 2; xfail 0; xpass 0.")
  message(FATAL_ERROR "intentional nonzero")
endif()

if(_run STREQUAL "regressions/member_shared_setup_skip_measured/bench_member")
  message("Summary: passed 0/0; failed 1; skipped 0; xfail 0; xpass 0.")
  message(FATAL_ERROR "intentional nonzero")
endif()

message(FATAL_ERROR "Unexpected invocation. prog='${_prog}' run='${_run}'")
]=])

function(_seed_stale_xml _label _attrs)
  file(WRITE "${_out_dir}/${_label}.xml" "<testsuite stale=\"1\" ${_attrs}></testsuite>\n")
endfunction()

_seed_stale_xml(pass "tests=\"1\" failures=\"0\" skipped=\"0\" errors=\"0\"")
_seed_stale_xml(fail "tests=\"1\" failures=\"1\" skipped=\"0\" errors=\"0\"")
_seed_stale_xml(skip "tests=\"1\" failures=\"0\" skipped=\"1\" errors=\"0\"")
_seed_stale_xml(xfail "tests=\"1\" failures=\"0\" skipped=\"1\" errors=\"0\"")
_seed_stale_xml(xpass "tests=\"1\" failures=\"1\" skipped=\"0\" errors=\"0\"")
_seed_stale_xml(infra_skip_test "tests=\"2\" failures=\"2\" skipped=\"2\" errors=\"2\"")
_seed_stale_xml(infra_skip_measured "tests=\"1\" failures=\"1\" skipped=\"1\" errors=\"0\"")

execute_process(
  COMMAND
    "${CMAKE_COMMAND}"
    "-DPROG_UNIT=unit-prog"
    "-DPROG_OUTCOMES=outcomes-prog"
    "-DPROG_INFRA_TEST=infra-test-prog"
    "-DPROG_INFRA_MEASURED=infra-measured-prog"
    "-DEMU=${CMAKE_COMMAND};-P;${_fake_runner}"
    "-DOUT_DIR=${_out_dir}"
    -P
    "${_matrix_script}"
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

if(_rc EQUAL 0)
  message(FATAL_ERROR "Matrix script false-passed with stale JUnit XML and no fresh JUnit output")
endif()

set(_all "${_out}\n${_err}")
if(NOT _all MATCHES "expected JUnit file not found")
  message(FATAL_ERROR "Expected stale-JUnit failure reason not found. Output:\n${_all}")
endif()
