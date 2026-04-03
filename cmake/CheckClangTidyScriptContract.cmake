if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckClangTidyScriptContract.cmake: SOURCE_DIR not set")
endif()

set(_script_file "${SOURCE_DIR}/scripts/check_clang_tidy.sh")
set(_readme_file "${SOURCE_DIR}/README.md")
set(_agents_file "${SOURCE_DIR}/AGENTS.md")

foreach(_required_file IN ITEMS "${_script_file}" "${_readme_file}" "${_agents_file}")
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
string(REGEX MATCHALL "--line-filter=\"\\$\\{LINE_FILTER_JSON\\}\"" _line_filter_usages "${_script_content}")
list(LENGTH _line_filter_usages _line_filter_usage_count)
if(_line_filter_usage_count LESS 3)
  message(FATAL_ERROR
    "scripts/check_clang_tidy.sh must pass --line-filter=\"${LINE_FILTER_JSON}\" through both normal clang-tidy paths and the module path.")
endif()

file(READ "${_readme_file}" _readme_content)
string(FIND "${_readme_content}" "surfaces diagnostics from matching repo headers included by those translation units"
  _readme_contract_pos)
if(_readme_contract_pos EQUAL -1)
  message(FATAL_ERROR
    "README.md must describe the CI-aligned clang-tidy lane as covering matching repo headers included by the active translation units.")
endif()
string(FIND "${_readme_content}" "ninja clang-tidy" _readme_ninja_tidy_pos)
if(NOT _readme_ninja_tidy_pos EQUAL -1)
  message(FATAL_ERROR
    "README.md must not recommend a nonexistent 'ninja clang-tidy' target; point contributors to the tidy/tidy-fix preset build flow.")
endif()

file(READ "${_agents_file}" _agents_content)
string(FIND "${_agents_content}" "surfaces diagnostics from matching repo headers included by those translation units"
  _agents_contract_pos)
if(_agents_contract_pos EQUAL -1)
  message(FATAL_ERROR
    "AGENTS.md must describe the CI-aligned clang-tidy lane as covering matching repo headers included by the active translation units.")
endif()
string(FIND "${_agents_content}" "ninja clang-tidy" _agents_ninja_tidy_pos)
if(NOT _agents_ninja_tidy_pos EQUAL -1)
  message(FATAL_ERROR
    "AGENTS.md must not recommend a nonexistent 'ninja clang-tidy' target; point contributors to the tidy/tidy-fix preset build flow.")
endif()
