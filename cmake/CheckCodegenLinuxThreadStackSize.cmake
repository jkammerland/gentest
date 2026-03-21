if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenLinuxThreadStackSize.cmake: PROG not set")
endif()

if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
  message(STATUS "GENTEST_SKIP_TEST: codegen Linux thread-stack regression only applies on Linux hosts")
  return()
endif()

find_program(_readelf readelf)
if(NOT _readelf)
  message(STATUS "GENTEST_SKIP_TEST: readelf not available")
  return()
endif()

execute_process(
  COMMAND "${_readelf}" -lW "${PROG}"
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

if(NOT _rc EQUAL 0)
  message(FATAL_ERROR
    "readelf failed for '${PROG}' (rc=${_rc}).\nstdout:\n${_out}\nstderr:\n${_err}")
endif()

string(REGEX MATCH "GNU_STACK[^\n]*0x0*800000([^0-9A-Fa-f]|$)" _stack_match "${_out}")
if(_stack_match STREQUAL "")
  message(FATAL_ERROR
    "gentest_codegen should request an 8 MiB default Linux thread stack via GNU_STACK so musl workers do not inherit ~128 KiB stacks.\n"
    "readelf output:\n${_out}")
endif()

message(STATUS "gentest_codegen Linux thread-stack regression passed")
