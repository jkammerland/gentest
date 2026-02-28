# Verifies that runner-level infra failures are represented in Allure output
# alongside ordinary case results.
# Required:
#   PROG, OUT_DIR, PASS_CASE_NAME, INFRA_SUBSTRING
# Optional:
#   EXPECT_RC, ARGS, EMU

if(NOT DEFINED PROG)
  message(FATAL_ERROR "CheckAllureRunnerInfraParity.cmake: PROG not set")
endif()
if(NOT DEFINED OUT_DIR)
  message(FATAL_ERROR "CheckAllureRunnerInfraParity.cmake: OUT_DIR not set")
endif()
if(NOT DEFINED PASS_CASE_NAME)
  message(FATAL_ERROR "CheckAllureRunnerInfraParity.cmake: PASS_CASE_NAME not set")
endif()
if(NOT DEFINED INFRA_SUBSTRING)
  message(FATAL_ERROR "CheckAllureRunnerInfraParity.cmake: INFRA_SUBSTRING not set")
endif()

set(_emu)
if(DEFINED EMU)
  if(EMU MATCHES ";")
    set(_emu ${EMU})
  else()
    separate_arguments(_emu NATIVE_COMMAND "${EMU}")
  endif()
endif()

set(_args)
if(DEFINED ARGS)
  if(ARGS MATCHES ";")
    set(_args ${ARGS})
  else()
    separate_arguments(_args NATIVE_COMMAND "${ARGS}")
  endif()
endif()

file(MAKE_DIRECTORY "${OUT_DIR}")

execute_process(
  COMMAND ${_emu} "${PROG}" ${_args}
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

set(_all "${_out}\n${_err}")
if(DEFINED EXPECT_RC AND NOT "${EXPECT_RC}" STREQUAL "")
  if(NOT _rc EQUAL EXPECT_RC)
    message(FATAL_ERROR "Expected exit code ${EXPECT_RC}, got ${_rc}. Output:\n${_all}")
  endif()
endif()

file(GLOB _results "${OUT_DIR}/result-*-result.json")
list(LENGTH _results _result_count)
if(_result_count LESS 2)
  message(FATAL_ERROR "Expected at least 2 Allure result files, found ${_result_count}. Output:\n${_all}")
endif()

set(_found_case_pass OFF)
set(_found_infra_fail OFF)
foreach(_file IN LISTS _results)
  file(READ "${_file}" _content)

  string(REGEX MATCH "\"status\"[ \t\r\n]*:[ \t\r\n]*\"passed\"" _is_pass "${_content}")
  string(REGEX MATCH "\"status\"[ \t\r\n]*:[ \t\r\n]*\"failed\"" _is_fail "${_content}")

  string(FIND "${_content}" "${PASS_CASE_NAME}" _case_name_pos)
  if(NOT "${_is_pass}" STREQUAL "" AND NOT _case_name_pos EQUAL -1)
    set(_found_case_pass ON)
  endif()

  string(FIND "${_content}" "${INFRA_SUBSTRING}" _infra_pos)
  if(NOT "${_is_fail}" STREQUAL "" AND NOT _infra_pos EQUAL -1)
    set(_found_infra_fail ON)
  endif()
endforeach()

if(NOT _found_case_pass)
  message(FATAL_ERROR "Did not find passed Allure result for case '${PASS_CASE_NAME}'.")
endif()
if(NOT _found_infra_fail)
  message(FATAL_ERROR "Did not find failed Allure infra result containing '${INFRA_SUBSTRING}'.")
endif()
