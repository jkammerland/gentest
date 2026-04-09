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
  message(FATAL_ERROR "CheckCdashExperimentalLive.cmake: PROG not set")
endif()
if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckCdashExperimentalLive.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BINARY_ROOT)
  message(FATAL_ERROR "CheckCdashExperimentalLive.cmake: BINARY_ROOT not set")
endif()

set(_binary_dir "${BINARY_ROOT}/build")
file(REMOVE_RECURSE "${BINARY_ROOT}")

execute_process(
  COMMAND
    "${CMAKE_COMMAND}" -E env
    "GENTEST_CDASH_SOURCE_DIR=${SOURCE_DIR}"
    "GENTEST_CDASH_BINARY_DIR=${_binary_dir}"
    "GENTEST_CDASH_ENABLE_ALLURE_TESTS=OFF"
    "GENTEST_CDASH_BUILD_TARGETS=gentest_core_tests,gentest_unit_tests"
    "GENTEST_CDASH_TEST_REGEX=^(gentest_core_parse_validate|unit)$"
    "GENTEST_CDASH_PARALLEL_LEVEL=2"
    "${PROG}" -S "${SOURCE_DIR}/cmake/cdash/Experimental.cmake" -VV
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "CDash live smoke failed with code ${_rc}\nOutput:\n${_out}\nErrors:\n${_err}")
endif()

set(_all "${_out}\n${_err}")
foreach(_needle IN ITEMS
    "gentest-cdash:"
    "allure_tests=OFF"
    "build_targets=gentest_core_tests,gentest_unit_tests"
    "test_regex=^(gentest_core_parse_validate|unit)$"
    "Test project ${_binary_dir}"
    "gentest_core_parse_validate"
    "unit")
  _assert_contains("${_all}" "${_needle}" "Output")
endforeach()

set(_tag_file "${_binary_dir}/Testing/TAG")
if(NOT EXISTS "${_tag_file}")
  message(FATAL_ERROR "Missing Testing/TAG file: ${_tag_file}")
endif()
set(_cache_file "${_binary_dir}/CMakeCache.txt")
if(NOT EXISTS "${_cache_file}")
  message(FATAL_ERROR "Missing CMakeCache.txt: ${_cache_file}")
endif()
file(READ "${_cache_file}" _cache_text)
_assert_contains("${_cache_text}" "CMAKE_BUILD_TYPE:STRING=Debug" "CMakeCache.txt")
file(READ "${_tag_file}" _tag_text)
string(STRIP "${_tag_text}" _tag_text)
string(REPLACE "\n" ";" _tag_lines "${_tag_text}")
list(GET _tag_lines 0 _tag)
set(_test_xml "${_binary_dir}/Testing/${_tag}/Test.xml")
if(NOT EXISTS "${_test_xml}")
  message(FATAL_ERROR "Missing CDash Test.xml: ${_test_xml}")
endif()

message(STATUS "CDash live smoke passed")
