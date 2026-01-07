# Usage:
#   cmake -DPROG=<path> -DLINES=<n> [-DARGS="--flags ..."] -P cmake/CheckLines.cmake

if(NOT DEFINED PROG)
  message(FATAL_ERROR "PROG not set")
endif()
if(NOT DEFINED LINES)
  message(FATAL_ERROR "LINES not set")
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

execute_process(
  COMMAND ${_emu} "${PROG}" ${_args}
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err
  RESULT_VARIABLE rc
)

if(NOT rc EQUAL 0)
  message(FATAL_ERROR "Command failed with code ${rc}. Output:\n${out}\nErrors:\n${err}")
endif()

string(REGEX MATCHALL "\n" _lines "${out}")
list(LENGTH _lines line_count)
if(NOT out MATCHES "\n$")
  math(EXPR line_count "${line_count} + 1")
endif()

if(NOT line_count EQUAL LINES)
  message(FATAL_ERROR "Expected ${LINES} lines, got ${line_count}. Output:\n${out}")
endif()
