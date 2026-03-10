# Requires:
#  -DSOURCE_DIR=<path to fixture project>
#  -DBUILD_ROOT=<path to parent build dir>
#  -DGENERATOR=<cmake generator name>
# Optional:
#  -DGENERATOR_PLATFORM=<platform>
#  -DGENERATOR_TOOLSET=<toolset>
#  -DTOOLCHAIN_FILE=<toolchain>
#  -DMAKE_PROGRAM=<make/ninja path>
#  -DC_COMPILER=<C compiler>
#  -DCXX_COMPILER=<C++ compiler>
#  -DBUILD_TYPE=<Debug/Release/...>
#  -DBUILD_CONFIG=<Debug/Release/...>   # for multi-config generators

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckDiscoverTests.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT)
  message(FATAL_ERROR "CheckDiscoverTests.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENERATOR)
  message(FATAL_ERROR "CheckDiscoverTests.cmake: GENERATOR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")

set(_work_dir "${BUILD_ROOT}/discover_tests")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

set(_build_dir "${_work_dir}/build")

set(_cmake_gen_args -G "${GENERATOR}")
if(DEFINED GENERATOR_PLATFORM AND NOT "${GENERATOR_PLATFORM}" STREQUAL "")
  list(APPEND _cmake_gen_args -A "${GENERATOR_PLATFORM}")
endif()
if(DEFINED GENERATOR_TOOLSET AND NOT "${GENERATOR_TOOLSET}" STREQUAL "")
  list(APPEND _cmake_gen_args -T "${GENERATOR_TOOLSET}")
endif()

set(_cmake_cache_args)
if(DEFINED TOOLCHAIN_FILE AND NOT "${TOOLCHAIN_FILE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}")
endif()
if(DEFINED MAKE_PROGRAM AND NOT "${MAKE_PROGRAM}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()
if(DEFINED C_COMPILER AND NOT "${C_COMPILER}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_C_COMPILER=${C_COMPILER}")
endif()
if(DEFINED CXX_COMPILER AND NOT "${CXX_COMPILER}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}")
endif()
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()

message(STATUS "Configure gentest_discover_tests fixture...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${SOURCE_DIR}"
    -B "${_build_dir}"
    ${_cmake_cache_args}
  STRIP_TRAILING_WHITESPACE
  WORKING_DIRECTORY "${_work_dir}"
)

message(STATUS "Build gentest_discover_tests fixture...")
set(_build_args --build "${_build_dir}")
if(DEFINED BUILD_CONFIG AND NOT "${BUILD_CONFIG}" STREQUAL "")
  list(APPEND _build_args --config "${BUILD_CONFIG}")
endif()
gentest_check_run_or_fail(COMMAND "${CMAKE_COMMAND}" ${_build_args} STRIP_TRAILING_WHITESPACE WORKING_DIRECTORY "${_work_dir}")

set(_ctest_cmd "${CMAKE_CTEST_COMMAND}")
set(_ctest_common_args --output-on-failure)
if(DEFINED BUILD_CONFIG AND NOT "${BUILD_CONFIG}" STREQUAL "")
  list(APPEND _ctest_common_args -C "${BUILD_CONFIG}")
endif()

message(STATUS "List discovered tests...")
gentest_check_run_or_fail(
  COMMAND "${_ctest_cmd}" -N ${_ctest_common_args}
  STRIP_TRAILING_WHITESPACE
  WORKING_DIRECTORY "${_build_dir}"
  OUTPUT_VARIABLE _list_out
)

set(_expected_tests
  "demo/a"
  "demo/b"
  "demo/skip"
  "demo/has [bracket]"
  "death/demo/death")

string(REPLACE "\r" "" _list_out_normalized "${_list_out}")
string(REPLACE "\n" ";" _list_lines "${_list_out_normalized}")
set(_discovered_tests)
foreach(_line IN LISTS _list_lines)
  if(_line MATCHES "^[ \t]*Test #[0-9]+: (.+)$")
    list(APPEND _discovered_tests "${CMAKE_MATCH_1}")
  endif()
endforeach()

list(LENGTH _discovered_tests _discovered_count)
list(LENGTH _expected_tests _expected_count)
if(NOT _discovered_count EQUAL _expected_count)
  message(FATAL_ERROR
    "Unexpected discovered test count. Expected ${_expected_count}, got ${_discovered_count}. ctest -N output:\n${_list_out}")
endif()

list(SORT _discovered_tests)
list(SORT _expected_tests)
if(NOT _discovered_tests STREQUAL _expected_tests)
  string(JOIN "\n  " _expected_block ${_expected_tests})
  string(JOIN "\n  " _actual_block ${_discovered_tests})
  message(FATAL_ERROR
    "Discovered test set mismatch.\nExpected:\n  ${_expected_block}\nActual:\n  ${_actual_block}\nFull ctest -N output:\n${_list_out}")
endif()

if(_list_out MATCHES "(^|\\n)[ \t]*Test #[0-9]+: demo/death([ \t]*$)")
  message(FATAL_ERROR "Death test should not be registered as a normal test: 'demo/death'. ctest -N output:\n${_list_out}")
endif()

message(STATUS "Run discovered tests...")
gentest_check_run_or_fail(
  COMMAND "${_ctest_cmd}" ${_ctest_common_args} -R "^demo/"
  STRIP_TRAILING_WHITESPACE
  WORKING_DIRECTORY "${_build_dir}"
)

message(STATUS "Run discovered death tests...")
gentest_check_run_or_fail(
  COMMAND "${_ctest_cmd}" -V ${_ctest_common_args} -R "^death/"
  STRIP_TRAILING_WHITESPACE
  WORKING_DIRECTORY "${_build_dir}"
  OUTPUT_VARIABLE _death_out
)
string(FIND "${_death_out}" "Death test passed" _pos)
if(_pos EQUAL -1)
  message(FATAL_ERROR "Expected death harness success message. Output:\n${_death_out}")
endif()

message(STATUS "gentest_discover_tests fixture passed")
