if(NOT DEFINED PROG)
  message(FATAL_ERROR "CheckMeasuredCsvReport.cmake: PROG not set")
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

if(DEFINED EXPECT_RC)
  if(NOT "${rc}" MATCHES "^-?[0-9]+$")
    message(FATAL_ERROR "Expected numeric exit code ${EXPECT_RC}, got '${rc}'. Stdout:\n${out}\nStderr:\n${err}")
  endif()
  if(NOT rc EQUAL EXPECT_RC)
    message(FATAL_ERROR "Expected exit code ${EXPECT_RC}, got ${rc}. Stdout:\n${out}\nStderr:\n${err}")
  endif()
endif()

string(REGEX REPLACE "\r\n" "\n" _out "${out}")
string(REPLACE "\n" ";" _lines "${_out}")
list(LENGTH _lines _line_count)
if(_line_count LESS 2)
  message(FATAL_ERROR "Expected CSV header and at least one data row. Stdout:\n${out}\nStderr:\n${err}")
endif()

list(GET _lines 0 _header)
if(NOT "${_header}" STREQUAL "report,table,row,field,type,value")
  message(FATAL_ERROR "Unexpected CSV header '${_header}'. Stdout:\n${out}")
endif()

string(FIND "${out}" "# " _comment_pos)
if(NOT _comment_pos EQUAL -1)
  message(FATAL_ERROR "CSV report should not contain comment/title rows. Stdout:\n${out}")
endif()

foreach(_line IN LISTS _lines)
  if("${_line}" STREQUAL "")
    message(FATAL_ERROR "CSV report should not contain blank separator rows. Stdout:\n${out}")
  endif()
  string(REGEX REPLACE "[^,]" "" _commas "${_line}")
  string(LENGTH "${_commas}" _comma_count)
  if(NOT _comma_count EQUAL 5)
    message(FATAL_ERROR "Expected uniform six-column CSV row, got ${_comma_count} comma(s): '${_line}'. Stdout:\n${out}")
  endif()
endforeach()

if(DEFINED REQUIRED_STDOUT_SUBSTRING AND NOT "${REQUIRED_STDOUT_SUBSTRING}" STREQUAL "")
  string(FIND "${out}" "${REQUIRED_STDOUT_SUBSTRING}" _required_stdout_pos)
  if(_required_stdout_pos EQUAL -1)
    message(FATAL_ERROR "Expected stdout substring not found: '${REQUIRED_STDOUT_SUBSTRING}'. Stdout:\n${out}")
  endif()
endif()

if(DEFINED FORBID_STDOUT_SUBSTRING AND NOT "${FORBID_STDOUT_SUBSTRING}" STREQUAL "")
  string(FIND "${out}" "${FORBID_STDOUT_SUBSTRING}" _forbid_stdout_pos)
  if(NOT _forbid_stdout_pos EQUAL -1)
    message(FATAL_ERROR "Forbidden stdout substring found: '${FORBID_STDOUT_SUBSTRING}'. Stdout:\n${out}")
  endif()
endif()
