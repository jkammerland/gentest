# Requires:
#  -DSOURCE_DIR=<fixture source dir>
#  -DBUILD_ROOT=<path to parent build dir>
#  -DGENTEST_SOURCE_DIR=<path to gentest source tree>
#  -DGENERATOR=<cmake generator name>
# Optional:
#  -DGENERATOR_PLATFORM=<platform>
#  -DGENERATOR_TOOLSET=<toolset>
#  -DTOOLCHAIN_FILE=<toolchain.cmake>
#  -DMAKE_PROGRAM=<path>
#  -DC_COMPILER=<path>
#  -DCXX_COMPILER=<path>
#  -DBUILD_TYPE=<Debug|Release|...>

if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckFmtVersionMetadataGuard.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckFmtVersionMetadataGuard.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckFmtVersionMetadataGuard.cmake: GENTEST_SOURCE_DIR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")

set(_work_dir "${BUILD_ROOT}/fmt_version_metadata_guard")
set(_build_dir_ok "${_work_dir}/build-ok")
set(_build_dir_install_fail "${_work_dir}/build-install-fail")
set(_build_dir_install_ok "${_work_dir}/build-install-ok")
set(_install_prefix_ok "${_work_dir}/install-prefix")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

set(_cmake_gen_args -G "${GENERATOR}")
if(DEFINED GENERATOR_PLATFORM AND NOT "${GENERATOR_PLATFORM}" STREQUAL "")
  list(APPEND _cmake_gen_args -A "${GENERATOR_PLATFORM}")
endif()
if(DEFINED GENERATOR_TOOLSET AND NOT "${GENERATOR_TOOLSET}" STREQUAL "")
  list(APPEND _cmake_gen_args -T "${GENERATOR_TOOLSET}")
endif()

set(_common_cache_args
  "-DGENTEST_SOURCE_DIR=${GENTEST_SOURCE_DIR}")
if(DEFINED C_COMPILER AND NOT "${C_COMPILER}" STREQUAL "")
  list(APPEND _common_cache_args "-DCMAKE_C_COMPILER=${C_COMPILER}")
endif()
if(DEFINED CXX_COMPILER AND NOT "${CXX_COMPILER}" STREQUAL "")
  list(APPEND _common_cache_args "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}")
endif()
if(DEFINED MAKE_PROGRAM AND NOT "${MAKE_PROGRAM}" STREQUAL "")
  list(APPEND _common_cache_args "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()
if(DEFINED TOOLCHAIN_FILE AND NOT "${TOOLCHAIN_FILE}" STREQUAL "")
  list(APPEND _common_cache_args "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}")
endif()
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _common_cache_args "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()

message(STATUS "Configure fmt metadata guard fixture with gentest_INSTALL=OFF...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${SOURCE_DIR}"
    -B "${_build_dir_ok}"
    ${_common_cache_args}
    "-Dgentest_INSTALL=OFF"
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

message(STATUS "Configure fmt metadata guard fixture with gentest_INSTALL=ON (expected failure)...")
execute_process(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${SOURCE_DIR}"
    -B "${_build_dir_install_fail}"
    ${_common_cache_args}
    "-Dgentest_INSTALL=ON"
  WORKING_DIRECTORY "${_work_dir}"
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

if(_rc EQUAL 0)
  message(FATAL_ERROR
    "fmt metadata guard fixture unexpectedly configured with gentest_INSTALL=ON even though the predefined fmt::fmt target has no version metadata")
endif()

set(_all "${_out}\n${_err}")
string(FIND "${_all}" "failed to determine resolved fmt version" _msg_pos)
if(_msg_pos EQUAL -1)
  message(FATAL_ERROR
    "Unexpected failure while probing fmt version metadata guard.\n"
    "stdout:\n${_out}\n"
    "stderr:\n${_err}")
endif()

message(STATUS "fmt metadata guard passed")

message(STATUS "Configure fmt metadata guard fixture with gentest_INSTALL=ON and explicit fmt_VERSION...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${SOURCE_DIR}"
    -B "${_build_dir_install_ok}"
    ${_common_cache_args}
    "-Dgentest_INSTALL=ON"
    "-Dfmt_VERSION=12.1.0"
    "-DCMAKE_INSTALL_PREFIX=${_install_prefix_ok}"
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

message(STATUS "Build install target for fmt metadata guard fixture...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    --build "${_build_dir_install_ok}"
    --target install
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

file(GLOB_RECURSE _config_candidates
  LIST_DIRECTORIES FALSE
  "${_install_prefix_ok}/*/gentestConfig.cmake"
  "${_install_prefix_ok}/*/*/gentestConfig.cmake")
if(NOT _config_candidates)
  message(FATAL_ERROR "fmt metadata guard did not install gentestConfig.cmake under '${_install_prefix_ok}'")
endif()
list(GET _config_candidates 0 _config_file)
file(READ "${_config_file}" _config_text)
string(FIND "${_config_text}" "find_dependency(fmt 12.1.0 EXACT CONFIG REQUIRED)" _config_dep_pos)
if(_config_dep_pos EQUAL -1)
  message(FATAL_ERROR
    "fmt metadata guard installed config must require the explicit fmt_VERSION value.\n"
    "Config file: ${_config_file}")
endif()

message(STATUS "fmt metadata guard passed")
