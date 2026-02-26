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
#  -DEXPECT_SUBSTRING=<expected configure error substring>

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckManifestNamedModuleReject.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT)
  message(FATAL_ERROR "CheckManifestNamedModuleReject.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENERATOR)
  message(FATAL_ERROR "CheckManifestNamedModuleReject.cmake: GENERATOR not set")
endif()
if(NOT DEFINED EXPECT_SUBSTRING)
  set(EXPECT_SUBSTRING "manifest mode does not support named module units")
endif()

set(_work_dir "${BUILD_ROOT}/manifest_named_module_reject")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

set(_build_fail_dir "${_work_dir}/build_fail")
set(_build_no_include_fail_dir "${_work_dir}/build_no_include_fail")

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

execute_process(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${SOURCE_DIR}"
    -B "${_build_fail_dir}"
    ${_cmake_cache_args}
    "-DUSE_NO_INCLUDE_SOURCES=OFF"
  WORKING_DIRECTORY "${_work_dir}"
  RESULT_VARIABLE _fail_rc
  OUTPUT_VARIABLE _fail_out
  ERROR_VARIABLE _fail_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

set(_fail_all "${_fail_out}\n${_fail_err}")
set(_fail_all_normalized "${_fail_all}")
string(REGEX REPLACE "[\r\n\t ]+" " " _fail_all_normalized "${_fail_all_normalized}")
if(_fail_rc EQUAL 0)
  message(FATAL_ERROR "Expected configure failure for manifest mode with named module units and source includes enabled.")
endif()
string(FIND "${_fail_all_normalized}" "${EXPECT_SUBSTRING}" _fail_pos)
if(_fail_pos EQUAL -1)
  message(FATAL_ERROR "Expected substring not found in configure output: '${EXPECT_SUBSTRING}'. Output:\n${_fail_all}")
endif()

execute_process(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${SOURCE_DIR}"
    -B "${_build_no_include_fail_dir}"
    ${_cmake_cache_args}
    "-DUSE_NO_INCLUDE_SOURCES=ON"
  WORKING_DIRECTORY "${_work_dir}"
  RESULT_VARIABLE _no_include_fail_rc
  OUTPUT_VARIABLE _no_include_fail_out
  ERROR_VARIABLE _no_include_fail_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

set(_no_include_fail_all "${_no_include_fail_out}\n${_no_include_fail_err}")
set(_no_include_fail_all_normalized "${_no_include_fail_all}")
string(REGEX REPLACE "[\r\n\t ]+" " " _no_include_fail_all_normalized "${_no_include_fail_all_normalized}")
if(_no_include_fail_rc EQUAL 0)
  message(FATAL_ERROR "Expected configure failure for manifest mode with named module units even with NO_INCLUDE_SOURCES.")
endif()
string(FIND "${_no_include_fail_all_normalized}" "${EXPECT_SUBSTRING}" _no_include_fail_pos)
if(_no_include_fail_pos EQUAL -1)
  message(FATAL_ERROR
    "Expected substring not found in configure output (NO_INCLUDE_SOURCES=ON): '${EXPECT_SUBSTRING}'. "
    "Output:\n${_no_include_fail_all}")
endif()

message(STATUS "Manifest named-module rejection check passed")
