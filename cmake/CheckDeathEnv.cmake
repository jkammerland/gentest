# Usage:
#   cmake -DPROG=<path> [-DARGS="--flags ..."] -DEXPECT_SUBSTRING=<text>
#         -DENV_VARS="VAR1=VALUE1;VAR2=VALUE2" -P cmake/CheckDeathEnv.cmake

if(NOT DEFINED PROG)
  message(FATAL_ERROR "CheckDeathEnv.cmake: PROG not set")
endif()

set(_args)
if(DEFINED ARGS)
  separate_arguments(_args NATIVE_COMMAND ${ARGS})
endif()

set(_env)
if(DEFINED ENV_VARS)
  foreach(kv IN LISTS ENV_VARS)
    list(APPEND _env "${kv}")
  endforeach()
endif()

execute_process(
  COMMAND ${CMAKE_COMMAND} -E env ${_env} ${PROG} ${_args}
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE
)

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

message(STATUS "Death test (with env) passed: non-zero exit and expected output present")
