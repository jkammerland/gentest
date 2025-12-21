# Usage:
#   cmake -DPROG=<path> -DFILE=<path> -DEXPECT_SUBSTRING=<text> [-DARGS="--flags ..."] -P cmake/CheckFileContains.cmake

if(NOT DEFINED PROG)
  message(FATAL_ERROR "PROG not set")
endif()
if(NOT DEFINED FILE)
  message(FATAL_ERROR "FILE not set")
endif()
if(NOT DEFINED EXPECT_SUBSTRING)
  message(FATAL_ERROR "EXPECT_SUBSTRING not set")
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
  COMMAND "${PROG}" ${_args}
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err
  RESULT_VARIABLE rc
)

if(NOT rc EQUAL 0)
  message(FATAL_ERROR "Command failed with code ${rc}. Output:\n${out}\nErrors:\n${err}")
endif()

if(NOT EXISTS "${FILE}")
  message(FATAL_ERROR "Expected file not found: ${FILE}")
endif()

file(READ "${FILE}" _content)
string(FIND "${_content}" "${EXPECT_SUBSTRING}" _pos)
if(_pos EQUAL -1)
  message(FATAL_ERROR "Expected substring not found in file: '${EXPECT_SUBSTRING}'. File: ${FILE}\nContent:\n${_content}")
endif()
