if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckCoverageWorkflowSerialCtest.cmake: SOURCE_DIR not set")
endif()

set(_workflow_file "${SOURCE_DIR}/.github/workflows/cmake.yml")
if(NOT EXISTS "${_workflow_file}")
  message(FATAL_ERROR "Missing workflow file: ${_workflow_file}")
endif()

file(READ "${_workflow_file}" _content)

string(FIND "${_content}" "build_type: coverage" _coverage_build_pos)
if(_coverage_build_pos EQUAL -1)
  message(FATAL_ERROR "Coverage matrix entry not found in ${_workflow_file}")
endif()

string(SUBSTRING "${_content}" ${_coverage_build_pos} -1 _coverage_tail)
string(FIND "${_coverage_tail}" "
          - name:" _next_row_rel)
if(_next_row_rel EQUAL -1)
  set(_coverage_row "${_coverage_tail}")
else()
  string(SUBSTRING "${_coverage_tail}" 0 ${_next_row_rel} _coverage_row)
endif()

string(FIND "${_coverage_row}" "run_coverage: true" _run_coverage_pos)
if(_run_coverage_pos EQUAL -1)
  message(FATAL_ERROR
    "Coverage matrix row must mark run_coverage: true.\n"
    "Observed row:\n${_coverage_row}")
endif()

string(FIND "${_coverage_row}" "ctest_parallel: 1" _ctest_parallel_pos)
if(_ctest_parallel_pos EQUAL -1)
  message(FATAL_ERROR
    "Coverage matrix row must set ctest_parallel: 1 so repeated coverage runs do not race on shared .gcda files.\n"
    "Observed row:\n${_coverage_row}")
endif()

set(_expected_test_line [=[ctest --preset=${GENTEST_CMAKE_PRESET} --output-on-failure --parallel ${{ matrix.ctest_parallel || 4 }}]=])
string(FIND "${_content}" "${_expected_test_line}" _test_line_pos)
if(_test_line_pos EQUAL -1)
  message(FATAL_ERROR
    "Workflow test step must use matrix.ctest_parallel so coverage jobs can force serial execution.\n"
    "Expected line:\n${_expected_test_line}")
endif()

set(_forbidden_gcov_pref [=[GCOV_CMD=(llvm-cov gcov)]=])
string(FIND "${_content}" "${_forbidden_gcov_pref}" _forbidden_gcov_pos)
if(NOT _forbidden_gcov_pos EQUAL -1)
  message(FATAL_ERROR
    "Coverage workflow must not prefer llvm-cov gcov here; the current coverage tree expects plain gcov.")
endif()

set(_expected_gcov_line [=[--gcov gcov]=])
string(FIND "${_content}" "${_expected_gcov_line}" _gcov_line_pos)
if(_gcov_line_pos EQUAL -1)
  message(FATAL_ERROR
    "Coverage workflow must pass '--gcov gcov' to coverage_hygiene.py.")
endif()
