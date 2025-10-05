# Requires:
#  -DPROG=<path to test binary>
#  -DARGS=<optional CLI args>
#  -DEXPECT_SUBSTRING=<substring expected in combined output>

if(NOT DEFINED PROG)
  message(FATAL_ERROR "CheckDeath.cmake: PROG not set")
endif()

set(_args)
if(DEFINED ARGS)
  separate_arguments(_args NATIVE_COMMAND ${ARGS})
endif()

execute_process(
  COMMAND ${PROG} ${_args}
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

set(_all "${_out}\n${_err}")

if(_rc EQUAL 0)
  message(FATAL_ERROR "Expected process to abort/exit non-zero, but exit code was 0. Output:\n${_all}")
endif()

if(DEFINED EXPECT_SUBSTRING)
  string(FIND "${_all}" "${EXPECT_SUBSTRING}" _pos)
  if(_pos EQUAL -1)
    message(FATAL_ERROR "Expected substring not found in output: '${EXPECT_SUBSTRING}'. Output:\n${_all}")
  endif()
endif()

message(STATUS "Death test passed (non-zero exit and expected output present)")
