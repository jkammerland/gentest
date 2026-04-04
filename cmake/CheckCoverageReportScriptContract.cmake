if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckCoverageReportScriptContract.cmake: SOURCE_DIR not set")
endif()

set(_script_file "${SOURCE_DIR}/scripts/coverage_report.py")
set(_config_file "${SOURCE_DIR}/scripts/coverage_hygiene.toml")
set(_readme_file "${SOURCE_DIR}/README.md")
set(_agents_file "${SOURCE_DIR}/AGENTS.md")
set(_doc_file "${SOURCE_DIR}/docs/coverage_hygiene.md")

foreach(_required_file IN ITEMS "${_script_file}" "${_config_file}" "${_readme_file}" "${_agents_file}" "${_doc_file}")
  if(NOT EXISTS "${_required_file}")
    message(FATAL_ERROR "Missing file for coverage report contract: ${_required_file}")
  endif()
endforeach()

execute_process(
  COMMAND python3 "${_script_file}" --help
  RESULT_VARIABLE _help_rc
  OUTPUT_VARIABLE _help_stdout
  ERROR_VARIABLE _help_stderr)
if(NOT _help_rc EQUAL 0)
  message(FATAL_ERROR
    "scripts/coverage_report.py --help failed.\n"
    "stdout:\n${_help_stdout}\n"
    "stderr:\n${_help_stderr}")
endif()

foreach(_required_help IN ITEMS "--build-dir" "--output-dir" "--fail-under-line" "--fail-under-branch")
  string(FIND "${_help_stdout}" "${_required_help}" _help_pos)
  if(_help_pos EQUAL -1)
    message(FATAL_ERROR
      "scripts/coverage_report.py --help must document ${_required_help}.")
  endif()
endforeach()

file(READ "${_script_file}" _script_content)
foreach(_required_token IN ITEMS "coverage-report" "summary.json" "summary.md" "index.html" "DEFAULT_POLICY_PATH")
  string(FIND "${_script_content}" "${_required_token}" _token_pos)
  if(_token_pos EQUAL -1)
    message(FATAL_ERROR
      "scripts/coverage_report.py must reference ${_required_token} so the repo-owned coverage contract remains stable.")
  endif()
endforeach()

file(READ "${_config_file}" _config_content)
foreach(_required_token IN ITEMS "[report]" "fail_under_line = 75.0" "fail_under_branch = 45.0")
  string(FIND "${_config_content}" "${_required_token}" _token_pos)
  if(_token_pos EQUAL -1)
    message(FATAL_ERROR
      "scripts/coverage_hygiene.toml must define ${_required_token} for the coverage report policy.")
  endif()
endforeach()

file(READ "${_readme_file}" _readme_content)
foreach(_required_token IN ITEMS "scripts/coverage_report.py" "cmake --preset=coverage-system" "ctest --preset=coverage-system --output-on-failure --parallel 1")
  string(FIND "${_readme_content}" "${_required_token}" _token_pos)
  if(_token_pos EQUAL -1)
    message(FATAL_ERROR
      "README.md must document the CI-aligned coverage flow with ${_required_token}.")
  endif()
endforeach()

file(READ "${_agents_file}" _agents_content)
foreach(_required_token IN ITEMS "scripts/coverage_report.py" "coverage-system" "coverage-report/summary.md")
  string(FIND "${_agents_content}" "${_required_token}" _token_pos)
  if(_token_pos EQUAL -1)
    message(FATAL_ERROR
      "AGENTS.md must describe the repo-owned coverage reporting flow with ${_required_token}.")
  endif()
endforeach()

file(READ "${_doc_file}" _doc_content)
foreach(_required_token IN ITEMS "not a general coverage-reporting tool" "scripts/coverage_report.py" "GitHub job summary")
  string(FIND "${_doc_content}" "${_required_token}" _token_pos)
  if(_token_pos EQUAL -1)
    message(FATAL_ERROR
      "docs/coverage_hygiene.md must explain the hygiene/reporting split and include ${_required_token}.")
  endif()
endforeach()
