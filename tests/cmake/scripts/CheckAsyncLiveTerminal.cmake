# Requires:
#  -DPROG=<path to async live test binary>
# Optional:
#  -DARGS=<optional CLI args>

if(NOT DEFINED PROG)
  message(FATAL_ERROR "CheckAsyncLiveTerminal.cmake: PROG not set")
endif()

if(WIN32)
  message(STATUS "GENTEST_SKIP_TEST: async live terminal check requires a Unix PTY helper")
  return()
endif()

if(DEFINED EMU AND NOT "${EMU}" STREQUAL "")
  message(STATUS "GENTEST_SKIP_TEST: async live terminal check is host-only")
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
  TIMEOUT 15
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err)

set(_all "${_out}\n${_err}")
string(ASCII 13 _cr)
string(ASCII 27 _esc)

if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "async live terminal command failed with rc=${_rc}. Output:\n${_all}")
endif()

foreach(_required IN ITEMS "SUSPENDED" "YIELDED" "RUNNING" "async_live_slow/panel/00_async_waits_for_sync" "async_live_slow/panel/05_waiting_on_driver")
  string(FIND "${_all}" "${_required}" _pos)
  if(_pos EQUAL -1)
    message(FATAL_ERROR "Expected terminal output substring not found: '${_required}'. Output:\n${_all}")
  endif()
endforeach()

foreach(_final_case IN ITEMS
    "async_live_slow/panel/00_async_"
    "async_live_slow/panel/01_sync_rel"
    "async_live_slow/panel/05_waiting_")
  string(LENGTH "${_final_case}" _needle_len)
  string(REPLACE "${_final_case}" "" _without_case "${_all}")
  string(LENGTH "${_all}" _all_len)
  string(LENGTH "${_without_case}" _without_len)
  math(EXPR _count "(${_all_len} - ${_without_len}) / ${_needle_len}")
  if(_count LESS 1)
    message(FATAL_ERROR "Expected terminal output to include '${_final_case}'. Output:\n${_all}")
  endif()
endforeach()

if(DEFINED EXPECT_LOG_TAIL AND EXPECT_LOG_TAIL)
  set(_tail_prefix "${_cr}${_esc}[2K  ")
  set(_found_tail FALSE)
  foreach(_tail_line IN ITEMS
      "short async case started"
      "short async case resumed"
      "medium async tick"
      "long async driver tick")
    string(FIND "${_all}" "${_tail_prefix}${_tail_line}" _log_tail_pos)
    if(NOT _log_tail_pos EQUAL -1)
      set(_found_tail TRUE)
      break()
    endif()
  endforeach()
  if(NOT _found_tail)
    message(FATAL_ERROR "Expected async live terminal output to include log tail lines. Output:\n${_all}")
  endif()
endif()

if(DEFINED FORBID_LOG_TAIL AND FORBID_LOG_TAIL)
  set(_tail_prefix "${_cr}${_esc}[2K  ")
  foreach(_tail_line IN ITEMS
      "short async case started"
      "short async case resumed"
      "medium async tick"
      "long async driver tick")
    string(FIND "${_all}" "${_tail_prefix}${_tail_line}" _log_tail_pos)
    if(NOT _log_tail_pos EQUAL -1)
      message(FATAL_ERROR "Expected async live terminal output to hide log tail lines. Output:\n${_all}")
    endif()
  endforeach()
endif()

foreach(_forbidden IN ITEMS "${_esc}[2J" "${_esc}[H" "${_esc}[r" "${_esc}[?1049")
  string(FIND "${_all}" "${_forbidden}" _forbidden_pos)
  if(NOT _forbidden_pos EQUAL -1)
    message(FATAL_ERROR "async live terminal output used forbidden full-screen escape sequence. Output:\n${_all}")
  endif()
endforeach()
