# Usage:
#   cmake -DPROG=<path> -DFILE=<path> -DEXPECT_SUBSTRING=<text> [-DEXPECT_RC=<int>] [-DARGS="--flags ..."]
#     -P cmake/RunAndCheckFile.cmake
#
# Runs the program with provided args.
# If EXPECT_RC is set, enforces that exact exit code; otherwise does not enforce an exit code.
# Then checks that FILE exists and contains EXPECT_SUBSTRING.

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
