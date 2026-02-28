# Requires:
#  -DPROG=<path to executable>
# Optional:
#  -DARGS=<optional CLI args>
#  -DEXPECT_RC=<expected numeric exit code>
#  -DEXPECT_SUBSTRING=<substring that must be present in combined output>
#  -DFORBID_SUBSTRING=<substring that must NOT be present in combined output>

if(NOT DEFINED PROG)
  message(FATAL_ERROR "CheckNoSubstring.cmake: PROG not set")
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
  RESULT_VARIABLE rc
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

set(_all "${out}\n${err}")

if(DEFINED EXPECT_RC AND NOT "${EXPECT_RC}" STREQUAL "")
  if(NOT rc EQUAL EXPECT_RC)
    message(FATAL_ERROR "Expected exit code ${EXPECT_RC}, got ${rc}. Output:\n${_all}")
  endif()
endif()

if(DEFINED EXPECT_SUBSTRING AND NOT "${EXPECT_SUBSTRING}" STREQUAL "")
  string(FIND "${_all}" "${EXPECT_SUBSTRING}" _expect_pos)
  if(_expect_pos EQUAL -1)
    message(FATAL_ERROR "Expected substring not found: '${EXPECT_SUBSTRING}'. Output:\n${_all}")
  endif()
endif()

if(DEFINED FORBID_SUBSTRING AND NOT "${FORBID_SUBSTRING}" STREQUAL "")
  string(FIND "${_all}" "${FORBID_SUBSTRING}" _forbid_pos)
  if(NOT _forbid_pos EQUAL -1)
    message(FATAL_ERROR "Forbidden substring found: '${FORBID_SUBSTRING}'. Output:\n${_all}")
  endif()
endif()
