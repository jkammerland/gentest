# Usage:
#   cmake -DPROG=<path> -DFILE=<path> -DFORMAT=<JUNIT|ALLURE> [-DITEM_NAME=<testcase name>]
#         [-DREQUIRED_SUBSTRING=<text>] [-DEXPECT_RC=<int>] [-DARGS="--flags ..."]
#     -P cmake/RunAndCheckReportTime.cmake
#
# Runs the program, then checks that FILE exists, optionally contains REQUIRED_SUBSTRING,
# and reports a positive time for the target item: JUnit's `time` attribute, or the
# Allure result's `stop - start` in milliseconds.

if(NOT DEFINED PROG)
  message(FATAL_ERROR "PROG not set")
endif()
if(NOT DEFINED FILE)
  message(FATAL_ERROR "FILE not set")
endif()
if(NOT DEFINED FORMAT)
  message(FATAL_ERROR "FORMAT not set")
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

# FILE may be a glob, since Allure names each result file after its run: matches left
# by an earlier run are removed first, and exactly one must exist afterwards.
if(FILE MATCHES "[*?]")
  file(GLOB _stale_matches LIST_DIRECTORIES false "${FILE}")
  if(_stale_matches)
    file(REMOVE ${_stale_matches})
  endif()
elseif(EXISTS "${FILE}")
  file(REMOVE "${FILE}")
endif()

execute_process(
  COMMAND ${_emu} "${PROG}" ${_args}
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err
  RESULT_VARIABLE rc
)

if(DEFINED EXPECT_RC AND NOT "${EXPECT_RC}" STREQUAL "")
  if(NOT rc EQUAL EXPECT_RC)
    message(FATAL_ERROR "Expected exit code ${EXPECT_RC}, got ${rc}. Output:\n${out}\nErrors:\n${err}")
  endif()
endif()

if(FILE MATCHES "[*?]")
  file(GLOB _matches LIST_DIRECTORIES false "${FILE}")
  list(LENGTH _matches _match_count)
  if(NOT _match_count EQUAL 1)
    message(FATAL_ERROR "Expected one file matching ${FILE}, found ${_match_count}: ${_matches}\nProgram exit code: ${rc}\nOutput:\n${out}\nErrors:\n${err}")
  endif()
  set(FILE "${_matches}")
endif()
if(NOT EXISTS "${FILE}")
  message(FATAL_ERROR "Expected file not found: ${FILE}\nProgram exit code: ${rc}\nOutput:\n${out}\nErrors:\n${err}")
endif()

file(READ "${FILE}" _content)
if(DEFINED REQUIRED_SUBSTRING AND NOT "${REQUIRED_SUBSTRING}" STREQUAL "")
  string(FIND "${_content}" "${REQUIRED_SUBSTRING}" _expect_pos)
  if(_expect_pos EQUAL -1)
    message(FATAL_ERROR "Expected substring not found in file: '${REQUIRED_SUBSTRING}'. File: ${FILE}\nContent:\n${_content}")
  endif()
endif()

string(TOUPPER "${FORMAT}" _format)
set(_time_value "")
if(_format STREQUAL "JUNIT")
  if(NOT DEFINED ITEM_NAME OR "${ITEM_NAME}" STREQUAL "")
    message(FATAL_ERROR "ITEM_NAME not set for JUNIT format")
  endif()
  string(FIND "${_content}" "name=\"${ITEM_NAME}\"" _name_pos)
  if(_name_pos EQUAL -1)
    message(FATAL_ERROR "JUnit testcase not found: ${ITEM_NAME}\nFile: ${FILE}\nContent:\n${_content}")
  endif()
  string(SUBSTRING "${_content}" ${_name_pos} -1 _tail)
  string(REGEX MATCH "time=\"([^\"]+)\"" _time_match "${_tail}")
  set(_time_value "${CMAKE_MATCH_1}")
elseif(_format STREQUAL "ALLURE")
  string(REGEX MATCH "\"start\":([0-9]+)" _start_match "${_content}")
  set(_allure_start "${CMAKE_MATCH_1}")
  string(REGEX MATCH "\"stop\":([0-9]+)" _stop_match "${_content}")
  set(_allure_stop "${CMAKE_MATCH_1}")
  if(_allure_start STREQUAL "" OR _allure_stop STREQUAL "")
    message(FATAL_ERROR "Allure start/stop not found in ${FILE}\nContent:\n${_content}")
  endif()
  math(EXPR _time_value "${_allure_stop} - ${_allure_start}")
  if(_time_value LESS_EQUAL 0)
    message(FATAL_ERROR "Allure duration must be positive, got ${_time_value} ms. File: ${FILE}\nContent:\n${_content}")
  endif()
else()
  message(FATAL_ERROR "Unsupported FORMAT '${FORMAT}'")
endif()

string(STRIP "${_time_value}" _time_value)
if("${_time_value}" STREQUAL "")
  message(FATAL_ERROR "Serialized time value not found in ${FILE}\nContent:\n${_content}")
endif()

if(_time_value MATCHES "^0+([.]0+)?([eE][+-]?0+)?$")
  message(FATAL_ERROR "Serialized time value must be positive, got '${_time_value}'. File: ${FILE}\nContent:\n${_content}")
endif()
