if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckCoverageWorkflowSerialCtest.cmake: SOURCE_DIR not set")
endif()

set(_workflow_file "${SOURCE_DIR}/.github/workflows/cmake.yml")
if(NOT EXISTS "${_workflow_file}")
  message(FATAL_ERROR "Missing workflow file: ${_workflow_file}")
endif()

file(READ "${_workflow_file}" _content)

string(REPLACE "\n" ";" _lines "${_content}")
set(_coverage_row "")
set(_current_row "")
foreach(_line IN LISTS _lines)
  if(_line MATCHES "^          - ")
    if(NOT _current_row STREQUAL "")
      string(FIND "${_current_row}" "build_type: coverage" _current_is_coverage)
      if(NOT _current_is_coverage EQUAL -1)
        set(_coverage_row "${_current_row}")
        break()
      endif()
    endif()
    set(_current_row "${_line}\n")
  elseif(NOT _current_row STREQUAL "")
    string(APPEND _current_row "${_line}\n")
  endif()
endforeach()

if(_coverage_row STREQUAL "" AND NOT _current_row STREQUAL "")
  string(FIND "${_current_row}" "build_type: coverage" _current_is_coverage)
  if(NOT _current_is_coverage EQUAL -1)
    set(_coverage_row "${_current_row}")
  endif()
endif()

if(_coverage_row STREQUAL "")
  message(FATAL_ERROR "Coverage matrix row not found in ${_workflow_file}")
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

string(FIND "${_content}" "--gcov " _gcov_flag_pos)
if(_gcov_flag_pos EQUAL -1)
  message(FATAL_ERROR
    "Coverage workflow must pass an explicit '--gcov <tool>' argument to coverage_hygiene.py.")
endif()
