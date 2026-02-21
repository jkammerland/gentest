# Requires:
#  -DPROG=<path to test binary>
# Optional:
#  -DARGS=<optional CLI args>
#  -DTIMEOUT_SEC=<seconds, default 3>
#  -DEXPECT_RC=<expected numeric exit code>

if(NOT DEFINED PROG)
  message(FATAL_ERROR "CheckNoTimeout.cmake: PROG not set")
endif()

set(_timeout 3)
if(DEFINED TIMEOUT_SEC AND NOT "${TIMEOUT_SEC}" STREQUAL "")
  set(_timeout "${TIMEOUT_SEC}")
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

execute_process(
  COMMAND ${_emu} "${PROG}" ${_args}
  TIMEOUT ${_timeout}
  RESULT_VARIABLE rc
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE
)

set(_all "${out}\n${err}")

if(NOT rc MATCHES "^-?[0-9]+$")
  message(FATAL_ERROR "Process did not complete within ${_timeout}s (rc='${rc}'). Output:\n${_all}")
endif()

if(DEFINED EXPECT_RC AND NOT "${EXPECT_RC}" STREQUAL "")
  if(NOT rc EQUAL EXPECT_RC)
    message(FATAL_ERROR "Expected exit code ${EXPECT_RC}, got ${rc}. Output:\n${_all}")
  endif()
endif()

message(STATUS "Process completed within timeout (rc=${rc})")
