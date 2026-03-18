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

string(FIND "${_content}" "run_coverage: true" _run_coverage_pos)
if(_run_coverage_pos EQUAL -1)
  message(FATAL_ERROR "Coverage matrix entry must mark run_coverage: true")
endif()

string(FIND "${_content}" "ctest_parallel: 1" _ctest_parallel_pos)
if(_ctest_parallel_pos EQUAL -1)
  message(FATAL_ERROR
    "Coverage matrix entry must set ctest_parallel: 1 so repeated coverage runs do not race on shared .gcda files")
endif()

set(_expected_test_line [=[ctest --preset=${GENTEST_CMAKE_PRESET} --output-on-failure --parallel ${{ matrix.ctest_parallel || 4 }}]=])
string(FIND "${_content}" "${_expected_test_line}" _test_line_pos)
if(_test_line_pos EQUAL -1)
  message(FATAL_ERROR
    "Workflow test step must use matrix.ctest_parallel so coverage jobs can force serial execution.\n"
    "Expected line:\n${_expected_test_line}")
endif()
