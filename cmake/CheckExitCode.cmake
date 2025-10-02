# Usage:
#   cmake -DPROG=<path> -DEXPECT_RC=<int> [-DARGS="--flags ..."] -P cmake/CheckExitCode.cmake

if(NOT DEFINED PROG)
  message(FATAL_ERROR "PROG not set")
endif()
if(NOT DEFINED EXPECT_RC)
  message(FATAL_ERROR "EXPECT_RC not set")
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

if(NOT rc EQUAL EXPECT_RC)
  message(FATAL_ERROR "Expected exit code ${EXPECT_RC}, got ${rc}. Output:\n${out}\nErrors:\n${err}")
endif()

