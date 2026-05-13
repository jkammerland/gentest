# Requires:
#  -DPROG=<path to async live test binary>
# Optional:
#  -DARGS=<optional CLI args>

if(NOT DEFINED PROG)
  message(FATAL_ERROR "CheckAsyncPingPongTerminal.cmake: PROG not set")
endif()

if(WIN32)
  message(STATUS "GENTEST_SKIP_TEST: async ping-pong terminal check requires a Unix PTY helper")
  return()
endif()

if(DEFINED EMU AND NOT "${EMU}" STREQUAL "")
  message(STATUS "GENTEST_SKIP_TEST: async ping-pong terminal check is host-only")
  return()
endif()

find_program(SCRIPT_EXECUTABLE script)
if(NOT SCRIPT_EXECUTABLE)
  message(STATUS "GENTEST_SKIP_TEST: script(1) not found")
  return()
endif()

execute_process(
  COMMAND "${SCRIPT_EXECUTABLE}" --version
  RESULT_VARIABLE _version_rc
  OUTPUT_VARIABLE _version_out
  ERROR_VARIABLE _version_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
set(_version_all "${_version_out}\n${_version_err}")
if(NOT _version_rc EQUAL 0 OR NOT _version_all MATCHES "util-linux")
  message(STATUS "GENTEST_SKIP_TEST: script(1) does not look like util-linux script")
  return()
endif()

set(_args)
if(DEFINED ARGS)
  if(ARGS MATCHES ";")
    set(_args ${ARGS})
  else()
    separate_arguments(_args NATIVE_COMMAND "${ARGS}")
  endif()
endif()

set(_command "\"${PROG}\"")
foreach(_arg IN LISTS _args)
  string(REPLACE "\\" "\\\\" _escaped_arg "${_arg}")
  string(REPLACE "\"" "\\\"" _escaped_arg "${_escaped_arg}")
  string(APPEND _command " \"${_escaped_arg}\"")
endforeach()

execute_process(
  COMMAND ${CMAKE_COMMAND} -E env --unset=NO_COLOR --unset=GENTEST_NO_COLOR TERM=xterm-256color "${SCRIPT_EXECUTABLE}" -q -e -c "${_command}" /dev/null
  TIMEOUT 20
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err)

set(_all "${_out}\n${_err}")

if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "async ping-pong terminal command failed with rc=${_rc}. Output:\n${_all}")
endif()

foreach(_required IN ITEMS
    "async_live_slow/pingpong/00_ping :: 8 log(s)"
    "async_live_slow/pingpong/01_pong :: 8 log(s)"
    "Summary: passed 2/2; failed 0; skipped 0; blocked 0; xfail 0; xpass 0.")
  string(FIND "${_all}" "${_required}" _pos)
  if(_pos EQUAL -1)
    message(FATAL_ERROR "Expected terminal output substring not found: '${_required}'. Output:\n${_all}")
  endif()
endforeach()

foreach(_forbidden IN ITEMS
    "async_live_slow/pingpong/00_ping count"
    "async_live_slow/pingpong/01_pong count")
  string(FIND "${_all}" "${_forbidden}" _pos)
  if(NOT _pos EQUAL -1)
    message(FATAL_ERROR "Async live log tail was disabled, but streamed stdout logs leaked into terminal output: '${_forbidden}'. Output:\n${_all}")
  endif()
endforeach()
