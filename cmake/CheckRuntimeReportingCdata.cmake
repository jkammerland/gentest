# Usage:
#   cmake -DPROG=<path> -DJUNIT_FILE=<path> [-DARGS="..."] -DEXPECT_RC=<int> -P cmake/CheckRuntimeReportingCdata.cmake
#
# Runs PROG, verifies exit code, then checks that JUnit output does not contain
# an unsplit CDATA terminator sequence from the known regression marker.

if(NOT DEFINED PROG)
  message(FATAL_ERROR "PROG not set")
endif()
if(NOT DEFINED JUNIT_FILE)
  message(FATAL_ERROR "JUNIT_FILE not set")
endif()
if(NOT DEFINED EXPECT_RC)
  message(FATAL_ERROR "EXPECT_RC not set")
endif()

set(_arg_list)
if(DEFINED ARGS AND NOT "${ARGS}" STREQUAL "")
  if(ARGS MATCHES ";")
    set(_arg_list ${ARGS})
  else()
    separate_arguments(_arg_list NATIVE_COMMAND "${ARGS}")
  endif()
endif()

set(_emu)
if(DEFINED EMU)
  if(EMU MATCHES ";")
    set(_emu ${EMU})
  else()
    separate_arguments(_emu NATIVE_COMMAND "${EMU}")
  endif()
endif()

if(EXISTS "${JUNIT_FILE}")
  file(REMOVE "${JUNIT_FILE}")
endif()

execute_process(
  COMMAND ${_emu} "${PROG}" ${_arg_list}
  RESULT_VARIABLE rc
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

if(NOT rc EQUAL EXPECT_RC)
  message(FATAL_ERROR "Expected exit code ${EXPECT_RC}, got ${rc}. Output:\n${out}\nErrors:\n${err}")
endif()

if(NOT EXISTS "${JUNIT_FILE}")
  message(FATAL_ERROR "Expected JUnit file was not generated: ${JUNIT_FILE}\nOutput:\n${out}\nErrors:\n${err}")
endif()

file(READ "${JUNIT_FILE}" _xml)
string(FIND "${_xml}" "runtime-reporting-cdata-token ]]> marker]]>" _raw_pos)
if(NOT _raw_pos EQUAL -1)
  message(FATAL_ERROR "Found unsplit CDATA terminator in JUnit output:\n${_xml}")
endif()

string(FIND "${_xml}" "runtime-reporting-cdata-token ]]]]><![CDATA[> marker" _split_pos)
if(_split_pos EQUAL -1)
  message(FATAL_ERROR "Expected split CDATA sequence not found in JUnit output:\n${_xml}")
endif()
