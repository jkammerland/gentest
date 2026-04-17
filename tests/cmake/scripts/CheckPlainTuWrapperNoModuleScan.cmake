# Requires:
#  -DBUILD_ROOT=<path to parent build dir>
#  -DGENTEST_SOURCE_DIR=<path to gentest source tree>
#  -DGENERATOR=<cmake generator name>
#  -DPROG=<path to gentest_codegen>
# Optional:
#  -DGENERATOR_PLATFORM=<platform>
#  -DGENERATOR_TOOLSET=<toolset>
#  -DTOOLCHAIN_FILE=<toolchain.cmake>
#  -DMAKE_PROGRAM=<path>
#  -DC_COMPILER=<path>
#  -DCXX_COMPILER=<path>
#  -DBUILD_TYPE=<Debug|Release|...>

if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckPlainTuWrapperNoModuleScan.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckPlainTuWrapperNoModuleScan.cmake: GENTEST_SOURCE_DIR not set")
endif()
if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckPlainTuWrapperNoModuleScan.cmake: PROG not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")

set(_work_dir "${BUILD_ROOT}/plain_tu_wrapper_no_module_scan")
set(_source_dir "${_work_dir}/src")
set(_build_dir "${_work_dir}/build")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_source_dir}")

file(TO_CMAKE_PATH "${GENTEST_SOURCE_DIR}" _gentest_source_dir)
file(TO_CMAKE_PATH "${PROG}" _gentest_codegen_prog)

file(CONFIGURE OUTPUT "${_source_dir}/CMakeLists.txt" CONTENT [=[
cmake_minimum_required(VERSION 3.31)
project(gentest_plain_wrapper_consumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(gentest_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(GENTEST_BUILD_CODEGEN OFF CACHE BOOL "" FORCE)
set(GENTEST_ENABLE_PUBLIC_MODULES AUTO CACHE STRING "" FORCE)
set(GENTEST_CODEGEN_EXECUTABLE "@_gentest_codegen_prog@" CACHE FILEPATH "" FORCE)
add_subdirectory("@_gentest_source_dir@" gentest-build)

add_executable(plain_tests test_main.cpp)
target_link_libraries(plain_tests PRIVATE gentest::gentest_main)
target_compile_features(plain_tests PRIVATE cxx_std_23)
gentest_attach_codegen(plain_tests)
]=] @ONLY)

file(WRITE "${_source_dir}/test_main.cpp" [=[
#include "gentest/assertions.h"

[[using gentest: test("plain/basic")]]
void plain_basic() {
    gentest::asserts::EXPECT_TRUE(true);
}
]=])

set(_cmake_gen_args -G "${GENERATOR}")
if(DEFINED GENERATOR_PLATFORM AND NOT "${GENERATOR_PLATFORM}" STREQUAL "")
  list(APPEND _cmake_gen_args -A "${GENERATOR_PLATFORM}")
endif()
if(DEFINED GENERATOR_TOOLSET AND NOT "${GENERATOR_TOOLSET}" STREQUAL "")
  list(APPEND _cmake_gen_args -T "${GENERATOR_TOOLSET}")
endif()

set(_cmake_cache_args)
if(DEFINED C_COMPILER AND NOT "${C_COMPILER}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_C_COMPILER=${C_COMPILER}")
endif()
if(DEFINED CXX_COMPILER AND NOT "${CXX_COMPILER}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}")
endif()
if(DEFINED MAKE_PROGRAM AND NOT "${MAKE_PROGRAM}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()
if(DEFINED TOOLCHAIN_FILE AND NOT "${TOOLCHAIN_FILE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}")
endif()
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()

message(STATUS "Configure plain-TU wrapper fixture...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${_source_dir}"
    -B "${_build_dir}"
    ${_cmake_cache_args}
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

set(_compdb "${_build_dir}/compile_commands.json")
if(NOT EXISTS "${_compdb}")
  message(FATAL_ERROR "Expected fixture to produce '${_compdb}'")
endif()

file(READ "${_compdb}" _compdb_content)
string(REGEX MATCHALL "\\{[^}]*tu_0000_test_main\\.gentest\\.cpp[^}]*\\}" _wrapper_entries "${_compdb_content}")
list(LENGTH _wrapper_entries _wrapper_entry_count)
if(NOT _wrapper_entry_count EQUAL 1)
  message(FATAL_ERROR "Expected exactly one generated plain-TU wrapper compile command, got ${_wrapper_entry_count}")
endif()
list(GET _wrapper_entries 0 _wrapper_entry)

foreach(_forbidden IN ITEMS "-fmodules-ts" "-fdeps-format=" "-fmodule-mapper=")
  string(FIND "${_wrapper_entry}" "${_forbidden}" _forbidden_pos)
  if(NOT _forbidden_pos EQUAL -1)
    message(FATAL_ERROR
      "Plain gentest TU wrapper must not enable CMake module scanning by default.\n"
      "Found '${_forbidden}' in compile command:\n${_wrapper_entry}")
  endif()
endforeach()

set(_ninja_file "${_build_dir}/build.ninja")
if(EXISTS "${_ninja_file}")
  file(READ "${_ninja_file}" _ninja_content)
  foreach(_forbidden IN ITEMS
      "tu_0000_test_main\\.gentest\\.cpp(\\.(o|obj))?\\.ddi"
      "tu_0000_test_main\\.gentest\\.cpp(\\.(o|obj))?\\.modmap")
    string(REGEX MATCH "${_forbidden}" _forbidden_match "${_ninja_content}")
    if(_forbidden_match)
      message(FATAL_ERROR
        "Plain gentest TU wrapper must not have CMake module-scanning build edges.\n"
        "Matched '${_forbidden_match}' in build.ninja")
    endif()
  endforeach()
endif()

message(STATUS "Plain gentest TU wrapper compile command has no module-scanning flags")
