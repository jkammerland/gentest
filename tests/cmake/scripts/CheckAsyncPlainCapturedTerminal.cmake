# Requires:
#  -DPROG=<path to async live test binary>
# Optional:
#  -DARGS=<optional CLI args>
#  -DCAPTURE_ENV=<CODEX_CI|CI|TERM_DUMB>

if(NOT DEFINED PROG)
  message(FATAL_ERROR "CheckAsyncPlainCapturedTerminal.cmake: PROG not set")
endif()

if(WIN32)
  message(STATUS "GENTEST_SKIP_TEST: async captured terminal check requires a Unix PTY helper")
  return()
endif()

if(DEFINED EMU AND NOT "${EMU}" STREQUAL "")
  message(STATUS "GENTEST_SKIP_TEST: async captured terminal check is host-only")
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

function(_gentest_shell_quote out_var value)
  string(REPLACE "'" [=['"'"']=] _quoted "${value}")
  set(${out_var} "'${_quoted}'" PARENT_SCOPE)
endfunction()

_gentest_shell_quote(_command "${PROG}")
foreach(_arg IN LISTS _args)
  _gentest_shell_quote(_quoted_arg "${_arg}")
  string(APPEND _command " ${_quoted_arg}")
endforeach()

set(_capture_env "${CAPTURE_ENV}")
if(_capture_env STREQUAL "")
  set(_capture_env "CODEX_CI")
endif()

set(_term "xterm-256color")
set(_plain_env)
if(_capture_env STREQUAL "CODEX_CI")
  list(APPEND _plain_env "CODEX_CI=1")
elseif(_capture_env STREQUAL "CI")
  list(APPEND _plain_env "CI=1")
elseif(_capture_env STREQUAL "TERM_DUMB")
  set(_term "dumb")
else()
  message(FATAL_ERROR "Unknown CAPTURE_ENV '${_capture_env}'")
endif()

execute_process(
  COMMAND ${CMAKE_COMMAND} -E env
          --unset=CI
          --unset=CODEX_CI
          --unset=NO_COLOR
          --unset=GENTEST_NO_COLOR
          --unset=GENTEST_ASYNC_LIVE
          --unset=GENTEST_NO_ASYNC_LIVE
          TERM=${_term}
          ${_plain_env}
          "${SCRIPT_EXECUTABLE}" -q -e -c "${_command}" /dev/null
  TIMEOUT 10
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err)

set(_all "${_out}\n${_err}")
string(ASCII 27 _esc)

if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "async captured terminal command failed with rc=${_rc}. Output:\n${_all}")
endif()

foreach(_required IN ITEMS
    "short async case started"
    "short async case resumed"
    "[ PASS ] async_live_slow/panel/02_short_pass"
    "Summary: passed 1/1; failed 0; skipped 0; blocked 0; xfail 0; xpass 0.")
  string(FIND "${_all}" "${_required}" _pos)
  if(_pos EQUAL -1)
    message(FATAL_ERROR "Expected captured terminal output substring not found: '${_required}'. Output:\n${_all}")
  endif()
endforeach()

string(FIND "${_all}" "${_esc}" _esc_pos)
if(NOT _esc_pos EQUAL -1)
  message(FATAL_ERROR "Captured async terminal output should be plain, but contained an escape character. Output:\n${_all}")
endif()
