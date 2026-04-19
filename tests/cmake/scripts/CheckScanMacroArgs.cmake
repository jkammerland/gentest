# Requires:
#  -DBUILD_ROOT=<top-level build tree>
#  -DGENTEST_SOURCE_DIR=<gentest source tree>
#  -DGENERATOR=<cmake generator>
# Optional:
#  -DGENERATOR_PLATFORM=<platform>
#  -DGENERATOR_TOOLSET=<toolset>
#  -DMAKE_PROGRAM=<make/ninja path>
#  -DC_COMPILER=<C compiler>
#  -DCXX_COMPILER=<C++ compiler>

if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckScanMacroArgs.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckScanMacroArgs.cmake: GENTEST_SOURCE_DIR not set")
endif()
if(NOT DEFINED GENERATOR OR "${GENERATOR}" STREQUAL "")
  message(FATAL_ERROR "CheckScanMacroArgs.cmake: GENERATOR not set")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")

set(_work_dir "${BUILD_ROOT}/scan_macro_args")
set(_source_dir "${_work_dir}/src")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_source_dir}")

file(TO_CMAKE_PATH "${GENTEST_SOURCE_DIR}/cmake/GentestCodegen.cmake" _gentest_codegen_cmake)
file(WRITE "${_source_dir}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.31)
project(gentest_scan_macro_args LANGUAGES CXX)

if(NOT DEFINED GENTEST_CODEGEN_CMAKE OR "${GENTEST_CODEGEN_CMAKE}" STREQUAL "")
  message(FATAL_ERROR "GENTEST_CODEGEN_CMAKE not set")
endif()
if(NOT DEFINED EXPECT_ACTIVE_DEBUG_CONFIG)
  message(FATAL_ERROR "EXPECT_ACTIVE_DEBUG_CONFIG not set")
endif()

include("${GENTEST_CODEGEN_CMAKE}")

set(_src "${CMAKE_CURRENT_BINARY_DIR}/case.cpp")
file(WRITE "${_src}" "int scan_macro_args_case = 0;\n")

add_library(scan_macro_args OBJECT "${_src}")
target_compile_definitions(scan_macro_args PRIVATE
  BASE_TARGET=1
  $<$<CONFIG:Debug>:GENEX_TARGET_DEBUG=1>)
set_target_properties(scan_macro_args PROPERTIES
  COMPILE_DEFINITIONS_DEBUG "TARGET_DEBUG_PROP=1"
  COMPILE_DEFINITIONS_RELEASE "TARGET_RELEASE_PROP=1")
set_source_files_properties("${_src}" PROPERTIES
  COMPILE_DEFINITIONS "BASE_SOURCE=1;$<$<CONFIG:Debug>:GENEX_SOURCE_DEBUG=1>"
  COMPILE_DEFINITIONS_DEBUG "SOURCE_DEBUG_PROP=1"
  COMPILE_DEFINITIONS_RELEASE "SOURCE_RELEASE_PROP=1")

function(_expect_contains list_name item)
  if(NOT "${item}" IN_LIST ${list_name})
    list(JOIN ${list_name} "\n  " _joined)
    message(FATAL_ERROR "Expected '${item}' in ${list_name}, got:\n  ${_joined}")
  endif()
endfunction()

function(_expect_absent list_name item)
  if("${item}" IN_LIST ${list_name})
    list(JOIN ${list_name} "\n  " _joined)
    message(FATAL_ERROR "Did not expect '${item}' in ${list_name}, got:\n  ${_joined}")
  endif()
endfunction()

_gentest_collect_scan_macro_args(scan_macro_args "${_src}" _args _has_genex)
if(_has_genex)
  message(FATAL_ERROR "Target/source generator-expression COMPILE_DEFINITIONS should be skipped, not reported as unsupported")
endif()
list(JOIN _args "\n" _joined_args)
if(_joined_args MATCHES "\\$<")
  message(FATAL_ERROR "Scan macro args retained a generator expression:\n${_joined_args}")
endif()

_expect_contains(_args "-DBASE_TARGET=1")
_expect_contains(_args "-DBASE_SOURCE=1")
_expect_absent(_args "-DGENEX_TARGET_DEBUG=1")
_expect_absent(_args "-DGENEX_SOURCE_DEBUG=1")
_expect_absent(_args "-DTARGET_RELEASE_PROP=1")
_expect_absent(_args "-DSOURCE_RELEASE_PROP=1")

if(EXPECT_ACTIVE_DEBUG_CONFIG)
  _expect_contains(_args "-DTARGET_DEBUG_PROP=1")
  _expect_contains(_args "-DSOURCE_DEBUG_PROP=1")
else()
  _expect_absent(_args "-DTARGET_DEBUG_PROP=1")
  _expect_absent(_args "-DSOURCE_DEBUG_PROP=1")
endif()

_gentest_collect_scan_macro_args(scan_macro_args "${_src}" _explicit_args _explicit_has_genex
  -DEXPLICIT_ARG=1
  "-D$<$<CONFIG:Debug>:EXPLICIT_GENEX_ARG=1>")
if(NOT _explicit_has_genex)
  message(FATAL_ERROR "Explicit CLANG_ARGS generator-expression macro state should still be reported")
endif()
_expect_contains(_explicit_args "-DEXPLICIT_ARG=1")
_expect_absent(_explicit_args "-DEXPLICIT_GENEX_ARG=1")
]=])

set(_cmake_common_args
  "-DGENTEST_CODEGEN_CMAKE=${_gentest_codegen_cmake}")
if(DEFINED C_COMPILER AND NOT "${C_COMPILER}" STREQUAL "")
  list(APPEND _cmake_common_args "-DCMAKE_C_COMPILER=${C_COMPILER}")
endif()
if(DEFINED CXX_COMPILER AND NOT "${CXX_COMPILER}" STREQUAL "")
  list(APPEND _cmake_common_args "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}")
endif()

set(_active_gen_args -G "${GENERATOR}")
if(DEFINED GENERATOR_PLATFORM AND NOT "${GENERATOR_PLATFORM}" STREQUAL "")
  list(APPEND _active_gen_args -A "${GENERATOR_PLATFORM}")
endif()
if(DEFINED GENERATOR_TOOLSET AND NOT "${GENERATOR_TOOLSET}" STREQUAL "")
  list(APPEND _active_gen_args -T "${GENERATOR_TOOLSET}")
endif()
if(DEFINED MAKE_PROGRAM AND NOT "${MAKE_PROGRAM}" STREQUAL "")
  list(APPEND _active_gen_args "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()

set(_active_generator_is_multi_config FALSE)
if(GENERATOR STREQUAL "Ninja Multi-Config" OR GENERATOR STREQUAL "Xcode" OR GENERATOR MATCHES "^Visual Studio")
  set(_active_generator_is_multi_config TRUE)
endif()
if(_active_generator_is_multi_config)
  set(_active_config_args -DEXPECT_ACTIVE_DEBUG_CONFIG=OFF)
else()
  set(_active_config_args
    -DEXPECT_ACTIVE_DEBUG_CONFIG=ON
    -DCMAKE_BUILD_TYPE=Debug)
endif()

gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_active_gen_args}
    -S "${_source_dir}"
    -B "${_work_dir}/active"
    ${_cmake_common_args}
    ${_active_config_args}
  STRIP_TRAILING_WHITESPACE
  WORKING_DIRECTORY "${_work_dir}")

if(NOT _active_generator_is_multi_config)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E capabilities
    RESULT_VARIABLE _capabilities_rc
    OUTPUT_VARIABLE _capabilities_json
    ERROR_VARIABLE _capabilities_err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)
  if(NOT _capabilities_rc EQUAL 0)
    message(FATAL_ERROR "Failed to query CMake generator capabilities.\n--- stderr ---\n${_capabilities_err}")
  endif()

  find_program(_ninja_program NAMES ninja ninja-build)
  if(_capabilities_json MATCHES "\"name\"[ \t\r\n]*:[ \t\r\n]*\"Ninja Multi-Config\"" AND _ninja_program)
    gentest_check_run_or_fail(
      COMMAND
        "${CMAKE_COMMAND}"
        -G "Ninja Multi-Config"
        "-DCMAKE_MAKE_PROGRAM=${_ninja_program}"
        -S "${_source_dir}"
        -B "${_work_dir}/ninja-multi"
        ${_cmake_common_args}
        -DEXPECT_ACTIVE_DEBUG_CONFIG=OFF
      STRIP_TRAILING_WHITESPACE
      WORKING_DIRECTORY "${_work_dir}")
  else()
    message(STATUS "Ninja Multi-Config generator or ninja program unavailable; skipped secondary multi-config scan macro check")
  endif()
endif()
