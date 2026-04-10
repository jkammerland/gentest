# Requires:
#  -DPROG=<path to ctest>
#  -DSOURCE_DIR=<repo root>
#  -DBINARY_ROOT=<temp root>

function(_assert_contains haystack needle label)
  string(FIND "${haystack}" "${needle}" _idx)
  if(_idx EQUAL -1)
    message(FATAL_ERROR "Expected substring not found: '${needle}'\n${label}:\n${haystack}")
  endif()
endfunction()

if(NOT DEFINED PROG)
  message(FATAL_ERROR "CheckCdashExperimentalDryRun.cmake: PROG not set")
endif()
if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckCdashExperimentalDryRun.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BINARY_ROOT)
  message(FATAL_ERROR "CheckCdashExperimentalDryRun.cmake: BINARY_ROOT not set")
endif()

set(_binary_dir "${BINARY_ROOT}/build")
file(REMOVE_RECURSE "${BINARY_ROOT}")

set(_submit_url "https://cdash.example.test/submit.php?project=gentest")
execute_process(
  COMMAND
    "${CMAKE_COMMAND}" -E env
    "GENTEST_CDASH_SOURCE_DIR=${SOURCE_DIR}"
    "GENTEST_CDASH_BINARY_DIR=${_binary_dir}"
    "GENTEST_CDASH_DRY_RUN=ON"
    "GENTEST_CDASH_PARALLEL_LEVEL=3"
    "GENTEST_CDASH_SUBMIT_URL=${_submit_url}"
    "${PROG}" -S "${SOURCE_DIR}/cmake/cdash/Experimental.cmake" -VV
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "CDash dry-run failed with code ${_rc}\nOutput:\n${_out}\nErrors:\n${_err}")
endif()

set(_all "${_out}\n${_err}")
foreach(_needle IN ITEMS
    "gentest-cdash:"
    "source=${SOURCE_DIR}"
    "binary=${_binary_dir}"
    "model=Experimental"
    "group=Experimental"
    "parallel_level=3"
    "allure_tests=OFF"
    "submit_url=${_submit_url}")
  _assert_contains("${_all}" "${_needle}" "Output")
endforeach()

set(_config_file "${SOURCE_DIR}/CTestConfig.cmake")
if(NOT EXISTS "${_config_file}")
  message(FATAL_ERROR "Missing CTestConfig.cmake: ${_config_file}")
endif()
file(READ "${_config_file}" _config_text)
foreach(_needle IN ITEMS "CTEST_PROJECT_NAME" "gentest" "CTEST_NIGHTLY_START_TIME" "GENTEST_CDASH_SUBMIT_URL")
  _assert_contains("${_config_text}" "${_needle}" "CTestConfig.cmake")
endforeach()

set(_dry_run_file "${_binary_dir}/cdash-dry-run.txt")
if(NOT EXISTS "${_dry_run_file}")
  message(FATAL_ERROR "Expected dry-run note was not written: ${_dry_run_file}")
endif()
file(READ "${_dry_run_file}" _dry_run_text)
foreach(_needle IN ITEMS "source=${SOURCE_DIR}" "binary=${_binary_dir}" "submit_url=${_submit_url}")
  _assert_contains("${_dry_run_text}" "${_needle}" "Dry-run file")
endforeach()

message(STATUS "CDash dry-run check passed")
