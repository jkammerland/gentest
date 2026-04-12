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
    "-DREQUIRED_SUBSTRING=needle"
    "-DARGS=-E\\;true"
    -P "${SOURCE_DIR}/tests/cmake/scripts/CheckFileContains.cmake"
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

if(_rc EQUAL 0)
  message(FATAL_ERROR "CheckFileContains.cmake accepted stale generated file contents")
endif()
