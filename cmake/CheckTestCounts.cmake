# Usage:
#   cmake -DPROG=<path> -DPASS=<n> -DFAIL=<n> -DSKIP=<n> -P cmake/CheckTestCounts.cmake
#   cmake -DPROG=<path> -DPASS=<n> -DFAIL=<n> -DSKIP=<n> [-DXFAIL=<n>] [-DXPASS=<n>] -P cmake/CheckTestCounts.cmake
#   cmake -DPROG=<path> -DLIST=ON -DCASES=<n> -P cmake/CheckTestCounts.cmake

if(NOT DEFINED PROG)
  message(FATAL_ERROR "PROG not set")
endif()

set(_emu)
if(DEFINED EMU)
  if(EMU MATCHES ";")
    set(_emu ${EMU}) # already a list
  else()
    separate_arguments(_emu NATIVE_COMMAND "${EMU}") # string
  endif()
endif()

set(_args)
if(DEFINED ARGS)
  if(ARGS MATCHES ";")
    set(_args ${ARGS}) # already a list
  else()
    separate_arguments(_args NATIVE_COMMAND "${ARGS}") # string
  endif()
endif()

if(LIST)
  list(APPEND _args --list)
endif()

execute_process(
  COMMAND ${_emu} "${PROG}" ${_args}
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err
  RESULT_VARIABLE rc
)

if(LIST)
  if(NOT DEFINED CASES)
    message(FATAL_ERROR "LIST mode requires CASES")
  endif()
  string(REGEX MATCHALL "\n" _lines "${out}")
  list(LENGTH _lines line_count)
  # If output doesn't end with newline, count last line
  if(NOT out MATCHES "\n$")
    math(EXPR line_count "${line_count} + 1")
  endif()
  if(NOT line_count EQUAL CASES)
    message(FATAL_ERROR "Expected ${CASES} listed cases, got ${line_count}. Output: \n${out}")
  endif()
  return()
endif()

if(NOT DEFINED PASS OR NOT DEFINED FAIL OR NOT DEFINED SKIP)
  message(FATAL_ERROR "Non-LIST mode requires PASS, FAIL, and SKIP")
endif()

set(all "${out}${err}")
string(REGEX MATCHALL "\\[ PASS \\]" _pass_matches "${all}")
list(LENGTH _pass_matches pass_count)
string(REGEX MATCHALL "\\[ FAIL \\]" _fail_matches "${all}")
list(LENGTH _fail_matches fail_count)
string(REGEX MATCHALL "\\[ SKIP \\]" _skip_matches "${all}")
list(LENGTH _skip_matches skip_count)
string(REGEX MATCHALL "\\[ XFAIL \\]" _xfail_matches "${all}")
list(LENGTH _xfail_matches xfail_count)
string(REGEX MATCHALL "\\[ XPASS \\]" _xpass_matches "${all}")
list(LENGTH _xpass_matches xpass_count)

set(_ok TRUE)
if(NOT pass_count EQUAL PASS)
  message(STATUS "PASS count mismatch: expected ${PASS}, got ${pass_count}")
  set(_ok FALSE)
endif()
if(NOT fail_count EQUAL FAIL)
  message(STATUS "FAIL count mismatch: expected ${FAIL}, got ${fail_count}")
  set(_ok FALSE)
endif()
if(NOT skip_count EQUAL SKIP)
  message(STATUS "SKIP count mismatch: expected ${SKIP}, got ${skip_count}")
  set(_ok FALSE)
endif()
if(DEFINED XFAIL AND NOT "${XFAIL}" STREQUAL "" AND NOT xfail_count EQUAL XFAIL)
  message(STATUS "XFAIL count mismatch: expected ${XFAIL}, got ${xfail_count}")
  set(_ok FALSE)
endif()
if(DEFINED XPASS AND NOT "${XPASS}" STREQUAL "" AND NOT xpass_count EQUAL XPASS)
  message(STATUS "XPASS count mismatch: expected ${XPASS}, got ${xpass_count}")
  set(_ok FALSE)
endif()

# Exit code validation: explicit EXPECT_RC overrides inferred behavior.
if(DEFINED EXPECT_RC AND NOT "${EXPECT_RC}" STREQUAL "")
  if(NOT rc EQUAL EXPECT_RC)
    message(STATUS "Exit code mismatch: expected ${EXPECT_RC}, got ${rc}")
    set(_ok FALSE)
  endif()
else()
  # When there are failures expect non-zero; otherwise 0.
  set(_fail_like ${FAIL})
  if(DEFINED XPASS AND NOT "${XPASS}" STREQUAL "")
    math(EXPR _fail_like "${_fail_like} + ${XPASS}")
  endif()

  if(_fail_like GREATER 0)
    if(rc EQUAL 0)
      message(STATUS "Expected non-zero exit code when failures present, got ${rc}")
      set(_ok FALSE)
    endif()
  else()
    if(NOT rc EQUAL 0)
      message(STATUS "Unexpected non-zero exit code: ${rc}")
      set(_ok FALSE)
    endif()
  endif()
endif()

if(NOT _ok)
  message(FATAL_ERROR "Counts did not match. Output:\n${out}\nErrors:\n${err}")
endif()
