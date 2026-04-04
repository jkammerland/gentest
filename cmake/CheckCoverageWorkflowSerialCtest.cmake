if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckCoverageWorkflowSerialCtest.cmake: SOURCE_DIR not set")
endif()

set(_workflow_file "${SOURCE_DIR}/.github/workflows/cmake.yml")
if(NOT EXISTS "${_workflow_file}")
  message(FATAL_ERROR "Missing workflow file: ${_workflow_file}")
endif()

file(READ "${_workflow_file}" _content)
string(FIND "${_content}" "  linux:" _build_test_job_start)
string(FIND "${_content}" "  package-workflow:" _package_workflow_start)
if(_build_test_job_start EQUAL -1 OR _package_workflow_start EQUAL -1 OR _package_workflow_start LESS _build_test_job_start)
  message(FATAL_ERROR "Unable to isolate the linux job block in ${_workflow_file}")
endif()
math(EXPR _build_test_job_len "${_package_workflow_start} - ${_build_test_job_start}")
string(SUBSTRING "${_content}" "${_build_test_job_start}" "${_build_test_job_len}" _build_test_job_content)

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

set(_expected_gcovr_bootstrap [=[python3 -m pip install --break-system-packages --upgrade gcovr==8.6]=])
string(FIND "${_content}" "${_expected_gcovr_bootstrap}" _gcovr_bootstrap_pos)
if(_gcovr_bootstrap_pos EQUAL -1)
  message(FATAL_ERROR
    "Coverage workflow must install gcovr before invoking scripts/coverage_report.py.\n"
    "Expected line:\n${_expected_gcovr_bootstrap}")
endif()

set(_expected_cleanup_dir [=[rm -rf "build/${GENTEST_CMAKE_PRESET}/coverage-report"]=])
string(FIND "${_content}" "${_expected_cleanup_dir}" _cleanup_dir_pos)
if(_cleanup_dir_pos EQUAL -1)
  message(FATAL_ERROR
    "Coverage workflow must clear the prior coverage-report directory before a run so failure-path publishing cannot reuse stale artifacts.")
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

set(_expected_summary_guard [=[if [ -f "${summary_file}" ]; then]=])
set(_expected_summary_append [=[cat "${summary_file}" >> "$GITHUB_STEP_SUMMARY"]=])
string(FIND "${_content}" "Coverage report was not generated because an earlier coverage step failed." _missing_summary_note_pos)
string(FIND "${_content}" "${_expected_summary_guard}" _summary_guard_pos)
string(FIND "${_content}" "${_expected_summary_append}" _summary_append_pos)
if(_missing_summary_note_pos EQUAL -1 OR _summary_guard_pos EQUAL -1 OR _summary_append_pos EQUAL -1)
  message(FATAL_ERROR
    "Coverage workflow must append the generated Markdown summary when present and publish a clear fallback note when report generation never produced summary.md.")
endif()
string(REGEX MATCHALL "if: \\$\\{\\{ always\\(\\) && matrix\\.run_coverage == true \\}\\}" _always_if_matches "${_content}")
list(LENGTH _always_if_matches _always_if_count)
if(_always_if_count LESS 2)
  message(FATAL_ERROR
    "Coverage summary/artifact publication must use always() on both publish steps so threshold failures still publish diagnostics.")
endif()

string(FIND "${_content}" "actions/upload-artifact@v6" _upload_artifact_pos)
if(_upload_artifact_pos EQUAL -1)
  message(FATAL_ERROR
    "Coverage workflow must upload the generated coverage report artifact with actions/upload-artifact@v6.")
endif()

set(_expected_artifact_path [=[path: build/${{ env.GENTEST_CMAKE_PRESET }}/coverage-report/]=])
string(FIND "${_content}" "${_expected_artifact_path}" _coverage_artifact_path_pos)
if(_coverage_artifact_path_pos EQUAL -1)
  message(FATAL_ERROR
    "Coverage workflow must upload the preset-derived coverage-report directory as a workflow artifact.")
endif()
string(FIND "${_content}" "if-no-files-found: ignore" _coverage_if_missing_ignore_pos)
if(_coverage_if_missing_ignore_pos EQUAL -1)
  message(FATAL_ERROR
    "Coverage workflow must ignore missing coverage-report artifacts so earlier failures do not add noisy secondary errors.")
endif()

foreach(_forbidden_token IN ITEMS "--roots " "--fail-on " "--warn-on " "--fail-under-line" "--fail-under-branch")
  string(FIND "${_build_test_job_content}" "${_forbidden_token}" _forbidden_pos)
  if(NOT _forbidden_pos EQUAL -1)
    message(FATAL_ERROR
      "Coverage workflow must keep repo coverage policy in scripts/coverage_hygiene.toml and scripts/coverage_report.py instead of overriding ${_forbidden_token} in the linux-build-test job.")
  endif()
endforeach()
string(REGEX MATCH "(^|\\n)[^\n]*gcovr[ \t-]" _raw_gcovr_match "${_build_test_job_content}")
if(NOT _raw_gcovr_match STREQUAL "")
  message(FATAL_ERROR
    "Coverage workflow must route report generation through scripts/coverage_report.py instead of invoking gcovr directly in the linux-build-test job.")
endif()
