# Requires:
#  -DSOURCE_DIR=<fixture source dir>
#  -DBUILD_ROOT=<path to parent build dir>
#  -DGENTEST_SOURCE_DIR=<path to gentest source tree>
# Optional:
#  -DGENERATOR=<cmake generator name>
#  -DGENERATOR_PLATFORM=<platform>
#  -DGENERATOR_TOOLSET=<toolset>
#  -DTOOLCHAIN_FILE=<toolchain.cmake>
#  -DMAKE_PROGRAM=<path>
#  -DC_COMPILER=<path>
#  -DCXX_COMPILER=<path>
#  -DBUILD_TYPE=<Debug|Release|...>

if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckMixedModuleMockRegistry.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckMixedModuleMockRegistry.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckMixedModuleMockRegistry.cmake: GENTEST_SOURCE_DIR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")

set(_work_dir "${BUILD_ROOT}/mixed_module_mock_registry")
set(_src_dir "${_work_dir}/src")
set(_build_dir "${_work_dir}/build")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(COPY "${SOURCE_DIR}/" DESTINATION "${_src_dir}")

find_program(_clang NAMES clang)
find_program(_clangxx NAMES clang++)
if(NOT _clang OR NOT _clangxx)
  message(STATUS "Skipping mixed module/non-module mock registry regression: clang/clang++ not found")
  return()
endif()

set(_cmake_gen_args -G "${GENERATOR}")
if(DEFINED GENERATOR_PLATFORM AND NOT "${GENERATOR_PLATFORM}" STREQUAL "")
  list(APPEND _cmake_gen_args -A "${GENERATOR_PLATFORM}")
endif()
if(DEFINED GENERATOR_TOOLSET AND NOT "${GENERATOR_TOOLSET}" STREQUAL "")
  list(APPEND _cmake_gen_args -T "${GENERATOR_TOOLSET}")
endif()

set(_cmake_cache_args
  "-DGENTEST_SOURCE_DIR=${GENTEST_SOURCE_DIR}"
  "-DCMAKE_C_COMPILER=${_clang}"
  "-DCMAKE_CXX_COMPILER=${_clangxx}")
if(DEFINED TOOLCHAIN_FILE AND NOT "${TOOLCHAIN_FILE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}")
endif()
if(DEFINED MAKE_PROGRAM AND NOT "${MAKE_PROGRAM}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()

message(STATUS "Configure mixed module/non-module mock registry fixture...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${_src_dir}"
    -B "${_build_dir}"
    ${_cmake_cache_args}
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

message(STATUS "Build mixed target with legacy and named-module mocks...")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --target mixed_tests
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

set(_generated_dir "${_build_dir}/generated")
set(_dispatcher_registry "${_generated_dir}/mixed_tests_mock_registry.hpp")
set(_dispatcher_impl "${_generated_dir}/mixed_tests_mock_impl.hpp")
set(_header_registry "${_generated_dir}/mixed_tests_mock_registry__domain_0000_header.hpp")
set(_header_impl "${_generated_dir}/mixed_tests_mock_impl__domain_0000_header.hpp")
set(_module_registry "${_generated_dir}/mixed_tests_mock_registry__domain_0001_gentest_mixed_module_cases.hpp")
set(_module_impl "${_generated_dir}/mixed_tests_mock_impl__domain_0001_gentest_mixed_module_cases.hpp")
set(_extra_registry "${_generated_dir}/mixed_tests_mock_registry__domain_0002_gentest_mixed_module_extra_cases.hpp")
set(_extra_impl "${_generated_dir}/mixed_tests_mock_impl__domain_0002_gentest_mixed_module_extra_cases.hpp")

foreach(_generated_file IN ITEMS
    "${_dispatcher_registry}"
    "${_dispatcher_impl}"
    "${_header_registry}"
    "${_header_impl}"
    "${_module_registry}"
    "${_module_impl}"
    "${_extra_registry}"
    "${_extra_impl}")
  if(NOT EXISTS "${_generated_file}")
    message(FATAL_ERROR "Expected generated mock artifact was not written: ${_generated_file}")
  endif()
endforeach()

file(READ "${_header_registry}" _header_registry_text)
file(READ "${_module_registry}" _module_registry_text)
file(READ "${_extra_registry}" _extra_registry_text)
file(READ "${_dispatcher_registry}" _dispatcher_registry_text)

string(FIND "${_header_registry_text}" "legacy::Service" _header_pos)
if(_header_pos EQUAL -1)
  message(FATAL_ERROR "Expected header-domain registry to contain legacy::Service")
endif()

string(FIND "${_module_registry_text}" "mixmod::Service" _module_pos)
if(_module_pos EQUAL -1)
  message(FATAL_ERROR "Expected module-domain registry to contain mixmod::Service")
endif()

string(FIND "${_extra_registry_text}" "extramod::Worker" _extra_pos)
if(_extra_pos EQUAL -1)
  message(FATAL_ERROR "Expected second module-domain registry to contain extramod::Worker")
endif()

string(FIND "${_dispatcher_registry_text}" "#else" _dispatcher_else_pos)
if(_dispatcher_else_pos EQUAL -1)
  message(FATAL_ERROR
    "Expected dispatcher registry to select either the source-local module domain or the header domain")
endif()

message(STATUS "Run mixed target acceptance cases...")
set(_prog "${_build_dir}/mixed_tests${CMAKE_EXECUTABLE_SUFFIX}")
gentest_check_run_or_fail(
  COMMAND "${_prog}" --run=mixed/legacy_mock
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)
gentest_check_run_or_fail(
  COMMAND "${_prog}" --run=mixed/module_mock
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)
gentest_check_run_or_fail(
  COMMAND "${_prog}" --run=mixed/extra_module_mock
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

message(STATUS "Observed expected mixed legacy/header and named-module mock success")
