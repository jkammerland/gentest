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

string(CONCAT _expected_reset_dir "build/" "$" "{GENTEST_CMAKE_PRESET}")
string(FIND "${_content}" "${_expected_reset_dir}" _preset_build_dir_pos)
if(_preset_build_dir_pos EQUAL -1)
  message(FATAL_ERROR
    "Coverage workflow must derive the coverage build directory from GENTEST_CMAKE_PRESET instead of hardcoding build/coverage.\n"
    "Expected literal path fragment:\n${_expected_reset_dir}")
endif()

string(CONCAT _expected_build_dir_arg "--build-dir \"build/" "$" "{GENTEST_CMAKE_PRESET}\"")
string(FIND "${_content}" "${_expected_build_dir_arg}" _coverage_build_dir_pos)
if(_coverage_build_dir_pos EQUAL -1)
  message(FATAL_ERROR
    "Coverage workflow must pass the preset-derived build directory to coverage_hygiene.py.\n"
    "Expected line fragment:\n${_expected_build_dir_arg}")
endif()

string(FIND "${_content}" "build/coverage" _hardcoded_coverage_dir_pos)
if(NOT _hardcoded_coverage_dir_pos EQUAL -1)
  message(FATAL_ERROR
    "Coverage workflow must not hardcode build/coverage; it should follow the selected preset build directory.")
endif()

set(_expected_gcov_args [=[--gcov llvm-cov gcov]=])
string(FIND "${_content}" "${_expected_gcov_args}" _gcov_flag_pos)
if(_gcov_flag_pos EQUAL -1)
  message(FATAL_ERROR
    "Coverage workflow must pass '--gcov llvm-cov gcov' so Clang coverage data is parsed by the matching LLVM gcov frontend.\n"
    "Expected line fragment:\n${_expected_gcov_args}")
endif()

string(FIND "${_content}" "--roots " _roots_override_pos)
if(NOT _roots_override_pos EQUAL -1)
  message(FATAL_ERROR
    "Coverage workflow should use coverage_hygiene.toml for source roots instead of overriding --roots in YAML.")
endif()

string(FIND "${_content}" "--fail-on " _fail_override_pos)
if(NOT _fail_override_pos EQUAL -1)
  message(FATAL_ERROR
    "Coverage workflow should use coverage_hygiene.toml for fail-on policy instead of overriding --fail-on in YAML.")
endif()

string(FIND "${_content}" "--warn-on " _warn_override_pos)
if(NOT _warn_override_pos EQUAL -1)
  message(FATAL_ERROR
    "Coverage workflow should use coverage_hygiene.toml for warn-on policy instead of overriding --warn-on in YAML.")
endif()

set(_expected_report_script [=[python3 scripts/coverage_report.py \]=])
string(FIND "${_content}" "${_expected_report_script}" _report_script_pos)
if(_report_script_pos EQUAL -1)
  message(FATAL_ERROR
    "Coverage workflow must invoke scripts/coverage_report.py from the Linux coverage lane.")
endif()

set(_expected_report_snippet [=[python3 scripts/coverage_report.py \
            --build-dir "build/${GENTEST_CMAKE_PRESET}"]=])
string(FIND "${_content}" "${_expected_report_snippet}" _report_build_dir_pos)
if(_report_build_dir_pos EQUAL -1)
  message(FATAL_ERROR
    "Coverage workflow must pass the preset-derived build directory to scripts/coverage_report.py.\n"
    "Expected snippet:\n${_expected_report_snippet}")
endif()

set(_expected_summary_line [=[cat "build/${GENTEST_CMAKE_PRESET}/coverage-report/summary.md" >> "$GITHUB_STEP_SUMMARY"]=])
string(FIND "${_content}" "${_expected_summary_line}" _summary_line_pos)
if(_summary_line_pos EQUAL -1)
  message(FATAL_ERROR
    "Coverage workflow must append the generated Markdown summary to GITHUB_STEP_SUMMARY.\n"
    "Expected line:\n${_expected_summary_line}")
endif()

string(FIND "${_content}" "actions/upload-artifact@v6" _upload_artifact_pos)
if(_upload_artifact_pos EQUAL -1)
  message(FATAL_ERROR
    "Coverage workflow must upload the generated coverage report artifact with actions/upload-artifact@v6.")
endif()

string(FIND "${_content}" "path: build/${{ env.GENTEST_CMAKE_PRESET }}/coverage-report/" _coverage_artifact_path_pos)
if(_coverage_artifact_path_pos EQUAL -1)
  message(FATAL_ERROR
    "Coverage workflow must upload the preset-derived coverage-report directory as a workflow artifact.")
endif()

string(FIND "${_content}" "gcovr " _raw_gcovr_pos)
if(NOT _raw_gcovr_pos EQUAL -1)
  message(FATAL_ERROR
    "Coverage workflow should route report generation through scripts/coverage_report.py instead of invoking gcovr directly in YAML.")
endif()

string(FIND "${_content}" "--fail-under-line" _raw_line_threshold_pos)
if(NOT _raw_line_threshold_pos EQUAL -1)
  message(FATAL_ERROR
    "Coverage workflow should take report thresholds from scripts/coverage_hygiene.toml via scripts/coverage_report.py, not inline YAML flags.")
endif()

string(FIND "${_content}" "--fail-under-branch" _raw_branch_threshold_pos)
if(NOT _raw_branch_threshold_pos EQUAL -1)
  message(FATAL_ERROR
    "Coverage workflow should take report thresholds from scripts/coverage_hygiene.toml via scripts/coverage_report.py, not inline YAML flags.")
endif()
