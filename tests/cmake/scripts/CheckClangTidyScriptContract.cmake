if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckClangTidyScriptContract.cmake: SOURCE_DIR not set")
endif()

set(_script_file "${SOURCE_DIR}/scripts/check_clang_tidy.sh")
set(_readme_file "${SOURCE_DIR}/README.md")
set(_contributing_file "${SOURCE_DIR}/CONTRIBUTING.md")
set(_agents_file "${SOURCE_DIR}/AGENTS.md")

foreach(_required_file IN ITEMS "${_script_file}" "${_readme_file}" "${_contributing_file}" "${_agents_file}")
  if(NOT EXISTS "${_required_file}")
    message(FATAL_ERROR "Missing file for clang-tidy script contract: ${_required_file}")
  endif()
endforeach()

file(READ "${_script_file}" _script_content)
if(_script_content MATCHES "printf '\\[\\{\\\"name\\\":\\\"%s\\\"")
  message(FATAL_ERROR
    "scripts/check_clang_tidy.sh must not rebuild the old single-source line filter.\n"
    "The CI-aligned clang-tidy lane is expected to keep repo-header diagnostics visible too.")
endif()
if(_script_content MATCHES "(^|[^A-Za-z0-9_])mapfile([^A-Za-z0-9_]|$)")
  message(FATAL_ERROR
    "scripts/check_clang_tidy.sh must stay compatible with the stock macOS Bash 3.2 shell and therefore must not use mapfile.")
endif()
if(_script_content MATCHES "wait[ \t]+-n")
  message(FATAL_ERROR
    "scripts/check_clang_tidy.sh must stay compatible with the stock macOS Bash 3.2 shell and therefore must not use wait -n.")
endif()

string(FIND "${_script_content}" "LINE_FILTER_JSON" _line_filter_json_pos)
if(_line_filter_json_pos EQUAL -1)
  message(FATAL_ERROR
    "scripts/check_clang_tidy.sh must keep the repo-wide line filter that includes tracked repo C/C++ files.")
endif()
string(FIND "${_script_content}" "safe.directory=" _safe_directory_pos)
if(_safe_directory_pos EQUAL -1)
  message(FATAL_ERROR
    "scripts/check_clang_tidy.sh must tolerate Git safe.directory checks in CI by passing a repo-local safe.directory override "
    "or equivalent fallback through the tracked-file discovery path.")
endif()
string(FIND "${_script_content}" "materializing generated targets" _materialize_generated_targets_pos)
if(_materialize_generated_targets_pos EQUAL -1)
  message(FATAL_ERROR
    "scripts/check_clang_tidy.sh must materialize generated mock/codegen targets from the active compile database "
    "before invoking clang-tidy so configure-only CI builds still have the generated surfaces they include.")
endif()
string(REGEX MATCHALL "--line-filter=\"\\$\\{LINE_FILTER_JSON\\}\"" _line_filter_usages "${_script_content}")
list(LENGTH _line_filter_usages _line_filter_usage_count)
if(_line_filter_usage_count LESS 3)
  message(FATAL_ERROR
    "scripts/check_clang_tidy.sh must pass --line-filter=\"${LINE_FILTER_JSON}\" through both normal clang-tidy paths and the module path.")
endif()

file(READ "${_readme_file}" _readme_content)
string(FIND "${_readme_content}" "CONTRIBUTING.md" _readme_contributing_pos)
if(_readme_contributing_pos EQUAL -1)
  message(FATAL_ERROR
    "README.md must point contributors to CONTRIBUTING.md for lint and static-analysis workflows.")
endif()
string(FIND "${_readme_content}" "ninja clang-tidy" _readme_ninja_tidy_pos)
if(NOT _readme_ninja_tidy_pos EQUAL -1)
  message(FATAL_ERROR
    "README.md must not recommend a nonexistent 'ninja clang-tidy' target; point contributors to the tidy/tidy-fix preset build flow.")
endif()

file(READ "${_contributing_file}" _contributing_content)
string(FIND "${_contributing_content}" "surfaces diagnostics from matching repo headers included by those translation units"
  _contributing_contract_pos)
if(_contributing_contract_pos EQUAL -1)
  message(FATAL_ERROR
    "CONTRIBUTING.md must describe the CI-aligned clang-tidy lane as covering matching repo headers included by the active translation units.")
endif()
string(FIND "${_contributing_content}" "materializes any generated mock/codegen targets referenced by the active compile database"
  _contributing_generated_contract_pos)
if(_contributing_generated_contract_pos EQUAL -1)
  message(FATAL_ERROR
    "CONTRIBUTING.md must note that scripts/check_clang_tidy.sh materializes generated mock/codegen targets referenced by the active compile database.")
endif()
string(FIND "${_contributing_content}" "ninja clang-tidy" _contributing_ninja_tidy_pos)
if(NOT _contributing_ninja_tidy_pos EQUAL -1)
  message(FATAL_ERROR
    "CONTRIBUTING.md must not recommend a nonexistent 'ninja clang-tidy' target; point contributors to the tidy/tidy-fix preset build flow.")
endif()

file(READ "${_agents_file}" _agents_content)
string(FIND "${_agents_content}" "surfaces diagnostics from matching repo headers included by those translation units"
  _agents_contract_pos)
if(_agents_contract_pos EQUAL -1)
  message(FATAL_ERROR
    "AGENTS.md must describe the CI-aligned clang-tidy lane as covering matching repo headers included by the active translation units.")
endif()
string(FIND "${_agents_content}" "materializes any generated mock/codegen targets referenced by the active compile database"
  _agents_generated_contract_pos)
if(_agents_generated_contract_pos EQUAL -1)
  message(FATAL_ERROR
    "AGENTS.md must note that scripts/check_clang_tidy.sh materializes generated mock/codegen targets referenced by the active compile database.")
endif()
string(FIND "${_agents_content}" "ninja clang-tidy" _agents_ninja_tidy_pos)
if(NOT _agents_ninja_tidy_pos EQUAL -1)
  message(FATAL_ERROR
    "AGENTS.md must not recommend a nonexistent 'ninja clang-tidy' target; point contributors to the tidy/tidy-fix preset build flow.")
endif()
