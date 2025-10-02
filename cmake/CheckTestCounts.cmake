# Usage:
#   cmake -DPROG=<path> -DPASS=<n> -DFAIL=<n> -DSKIP=<n> -P cmake/CheckTestCounts.cmake
#   cmake -DPROG=<path> -DLIST=ON -DCASES=<n> -P cmake/CheckTestCounts.cmake

if(NOT DEFINED PROG)
  message(FATAL_ERROR "PROG not set")
endif()

set(_args)
if(DEFINED ARGS)
  separate_arguments(_args NATIVE_COMMAND "${ARGS}")
endif()

if(LIST)
  list(APPEND _args --list)
endif()

execute_process(
  COMMAND "${PROG}" ${_args}
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

# Exit code validation: when there are failures expect non-zero; otherwise 0.
if(FAIL GREATER 0)
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

if(NOT _ok)
  message(FATAL_ERROR "Counts did not match. Output:\n${out}\nErrors:\n${err}")
endif()
