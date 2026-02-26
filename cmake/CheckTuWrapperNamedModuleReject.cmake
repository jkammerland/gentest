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
#  -DEXPECT_ALT_SUBSTRING=<optional alternate expected substring>

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckTuWrapperNamedModuleReject.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT)
  message(FATAL_ERROR "CheckTuWrapperNamedModuleReject.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENERATOR)
  message(FATAL_ERROR "CheckTuWrapperNamedModuleReject.cmake: GENERATOR not set")
endif()
if(NOT DEFINED EXPECT_SUBSTRING)
  set(EXPECT_SUBSTRING "TU wrapper mode does not support named module")
endif()
if(NOT DEFINED EXPECT_ALT_SUBSTRING)
  if(GENERATOR MATCHES "^Visual Studio" OR GENERATOR STREQUAL "Xcode" OR GENERATOR STREQUAL "Ninja Multi-Config")
    set(EXPECT_ALT_SUBSTRING "TU wrapper mode is not supported with multi-config generators")
  else()
    set(EXPECT_ALT_SUBSTRING "")
  endif()
endif()

set(_work_dir "${BUILD_ROOT}/tu_wrapper_named_module_reject")
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

execute_process(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${SOURCE_DIR}"
    -B "${_build_dir}"
    ${_cmake_cache_args}
  WORKING_DIRECTORY "${_work_dir}"
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

set(_all "${_out}\n${_err}")
set(_all_normalized "${_all}")
string(REGEX REPLACE "[\r\n\t ]+" " " _all_normalized "${_all_normalized}")
if(_rc EQUAL 0)
  message(FATAL_ERROR "Expected configure to fail for TU wrapper + named modules, but it succeeded.")
endif()

string(FIND "${_all_normalized}" "${EXPECT_SUBSTRING}" _pos)
set(_alt_pos -1)
if(NOT EXPECT_ALT_SUBSTRING STREQUAL "")
  string(FIND "${_all_normalized}" "${EXPECT_ALT_SUBSTRING}" _alt_pos)
endif()
if(_pos EQUAL -1 AND _alt_pos EQUAL -1)
  if(NOT EXPECT_ALT_SUBSTRING STREQUAL "")
    message(FATAL_ERROR
      "Expected substring not found in configure output: '${EXPECT_SUBSTRING}' or '${EXPECT_ALT_SUBSTRING}'. Output:\n${_all}")
  else()
    message(FATAL_ERROR "Expected substring not found in configure output: '${EXPECT_SUBSTRING}'. Output:\n${_all}")
  endif()
endif()

message(STATUS "Named module TU-wrapper rejection check passed")
