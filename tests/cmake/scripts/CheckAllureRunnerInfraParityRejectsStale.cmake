if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckAllureRunnerInfraParityRejectsStale.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BINARY_DIR)
  message(FATAL_ERROR "CheckAllureRunnerInfraParityRejectsStale.cmake: BINARY_DIR not set")
endif()

set(_out_dir "${BINARY_DIR}/allure-stale")
file(REMOVE_RECURSE "${_out_dir}")
file(MAKE_DIRECTORY "${_out_dir}")
file(WRITE "${_out_dir}/result-0-result.json" "{\"name\":\"pass\",\"status\":\"passed\"}\n")
file(WRITE "${_out_dir}/result-1-result.json" "{\"name\":\"infra boom\",\"status\":\"failed\"}\n")

execute_process(
  COMMAND
    "${CMAKE_COMMAND}"
    "-DPROG=${CMAKE_COMMAND}"
    "-DOUT_DIR=${_out_dir}"
    "-DPASS_CASE_NAME=pass"
    "-DINFRA_SUBSTRING=infra boom"
    "-DARGS=-E\\;true"
    -P "${SOURCE_DIR}/tests/cmake/scripts/CheckAllureRunnerInfraParity.cmake"
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

if(_rc EQUAL 0)
  message(FATAL_ERROR "CheckAllureRunnerInfraParity.cmake accepted stale Allure result files")
endif()
