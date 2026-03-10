if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckFileContainsRejectsStale.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BINARY_DIR)
  message(FATAL_ERROR "CheckFileContainsRejectsStale.cmake: BINARY_DIR not set")
endif()

set(_file "${BINARY_DIR}/stale-generated.txt")
file(REMOVE "${_file}")
file(MAKE_DIRECTORY "${BINARY_DIR}")
file(WRITE "${_file}" "needle\n")

execute_process(
  COMMAND
    "${CMAKE_COMMAND}"
    "-DPROG=${CMAKE_COMMAND}"
    "-DFILE=${_file}"
    "-DEXPECT_SUBSTRING=needle"
    "-DARGS=-E\\;true"
    -P "${SOURCE_DIR}/cmake/CheckFileContains.cmake"
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

if(_rc EQUAL 0)
  message(FATAL_ERROR "CheckFileContains.cmake accepted stale generated file contents")
endif()

set(_all "${_out}\n${_err}")
string(FIND "${_all}" "Expected file not found" _msg_pos)
if(_msg_pos EQUAL -1)
  message(FATAL_ERROR "Unexpected failure mode while rejecting stale generated file:\n${_all}")
endif()
