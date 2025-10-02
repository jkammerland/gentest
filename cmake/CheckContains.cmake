# Usage:
#   cmake -DPROG=<path> -DEXPECT_SUBSTRING=<text> [-DARGS="--flags ..."] -P cmake/CheckContains.cmake

if(NOT DEFINED PROG)
  message(FATAL_ERROR "PROG not set")
endif()
if(NOT DEFINED EXPECT_SUBSTRING)
  message(FATAL_ERROR "EXPECT_SUBSTRING not set")
endif()

set(_args)
if(DEFINED ARGS)
  separate_arguments(_args NATIVE_COMMAND "${ARGS}")
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

string(FIND "${out}" "${EXPECT_SUBSTRING}" _pos)
if(_pos EQUAL -1)
  message(FATAL_ERROR "Expected substring not found: '${EXPECT_SUBSTRING}'. Output:\n${out}")
endif()

