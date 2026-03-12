# Usage:
#   cmake -DPROG=<path> -DFILE=<path> -DEXPECT_SUBSTRING=<text> [-DEXPECT_RC=<int>] [-DFORBID_SUBSTRING=<text>]
#         [-DFORBID_SUBSTRINGS=<text|text>] [-DARGS="--flags ..."]
#     -P cmake/RunAndCheckFile.cmake
#
# Runs the program with provided args.
# If EXPECT_RC is set, enforces that exact exit code; otherwise does not enforce an exit code.
# Then checks that FILE exists, contains EXPECT_SUBSTRING, and does not contain any forbidden substring.

if(NOT DEFINED PROG)
  message(FATAL_ERROR "PROG not set")
endif()
if(NOT DEFINED FILE)
  message(FATAL_ERROR "FILE not set")
endif()
if(NOT DEFINED EXPECT_SUBSTRING)
  message(FATAL_ERROR "EXPECT_SUBSTRING not set")
endif()

set(_emu)
if(DEFINED EMU)
  if(EMU MATCHES ";")
    set(_emu ${EMU}) # already a list
  else()
    separate_arguments(_emu NATIVE_COMMAND "${EMU}") # string
  endif()
endif()

set(_args)
if(DEFINED ARGS)
  if(ARGS MATCHES ";")
    set(_args ${ARGS}) # already a list
  else()
    separate_arguments(_args NATIVE_COMMAND "${ARGS}") # string
  endif()
endif()

if(EXISTS "${FILE}")
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

if(NOT EXISTS "${FILE}")
  message(FATAL_ERROR "Expected file not found: ${FILE}\nProgram exit code: ${rc}\nOutput:\n${out}\nErrors:\n${err}")
endif()

file(READ "${FILE}" _content)
string(FIND "${_content}" "${EXPECT_SUBSTRING}" _pos)
if(_pos EQUAL -1)
  message(FATAL_ERROR "Expected substring not found in file: '${EXPECT_SUBSTRING}'. File: ${FILE}\nContent:\n${_content}")
endif()

set(_forbidden_substrings)
if(DEFINED FORBID_SUBSTRINGS AND NOT "${FORBID_SUBSTRINGS}" STREQUAL "")
  set(_forbid_values "${FORBID_SUBSTRINGS}")
  string(REPLACE "|" ";" _forbid_values "${_forbid_values}")
  list(APPEND _forbidden_substrings ${_forbid_values})
endif()
if(DEFINED FORBID_SUBSTRING AND NOT "${FORBID_SUBSTRING}" STREQUAL "")
  list(APPEND _forbidden_substrings "${FORBID_SUBSTRING}")
endif()

foreach(_forbid IN LISTS _forbidden_substrings)
  string(FIND "${_content}" "${_forbid}" _forbid_pos)
  if(NOT _forbid_pos EQUAL -1)
    message(FATAL_ERROR "Forbidden substring found in file: '${_forbid}'. File: ${FILE}\nContent:\n${_content}")
  endif()
endforeach()
