# Requires:
#  -DBUILD_ROOT=<path to build/tests root>
#  -DPACKAGE_NAME=<name>

if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckPackageConsumerProducerInvalidation.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED PACKAGE_NAME OR "${PACKAGE_NAME}" STREQUAL "")
  message(FATAL_ERROR "CheckPackageConsumerProducerInvalidation.cmake: PACKAGE_NAME not set")
endif()

function(_gentest_capture_producer_dir source_root out_var)
  set(options "")
  set(oneValueArgs TOOLCHAIN_FILE USE_MODULES INJECT_CODEGEN LINK_MODE)
  set(multiValueArgs EXTRA_CACHE_ARGS)
  cmake_parse_arguments(CAP "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT DEFINED CAP_USE_MODULES OR "${CAP_USE_MODULES}" STREQUAL "")
    set(CAP_USE_MODULES OFF)
  endif()
  if(NOT DEFINED CAP_INJECT_CODEGEN OR "${CAP_INJECT_CODEGEN}" STREQUAL "")
    set(CAP_INJECT_CODEGEN ON)
  endif()
  if(NOT DEFINED CAP_LINK_MODE OR "${CAP_LINK_MODE}" STREQUAL "")
    set(CAP_LINK_MODE main_only)
  endif()

  set(_toolchain_args)
  if(DEFINED CAP_TOOLCHAIN_FILE AND NOT "${CAP_TOOLCHAIN_FILE}" STREQUAL "")
    list(APPEND _toolchain_args "-DTOOLCHAIN_FILE=${CAP_TOOLCHAIN_FILE}")
  endif()

  execute_process(
    COMMAND
      "${CMAKE_COMMAND}"
      "-DSOURCE_DIR=${source_root}"
      "-DBUILD_ROOT=${BUILD_ROOT}"
      "-DPACKAGE_NAME=${PACKAGE_NAME}"
      "-DCONSUMER_LINK_MODE=${CAP_LINK_MODE}"
      "-DPACKAGE_TEST_USE_MODULES=${CAP_USE_MODULES}"
      "-DPACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE=${CAP_INJECT_CODEGEN}"
      "-DPACKAGE_TEST_DRY_RUN_PRODUCER_DIR=ON"
      ${_toolchain_args}
      ${CAP_EXTRA_CACHE_ARGS}
      -P "${CMAKE_CURRENT_LIST_DIR}/CheckPackageConsumer.cmake"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)

  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR
      "Failed to compute producer dir.\n--- stdout ---\n${_out}\n--- stderr ---\n${_err}")
  endif()

  set(_prefix "-- CheckPackageConsumer producer dir: ")
  string(FIND "${_out}" "${_prefix}" _prefix_pos)
  if(_prefix_pos EQUAL -1)
    message(FATAL_ERROR
      "Did not find producer dir marker.\n--- stdout ---\n${_out}\n--- stderr ---\n${_err}")
  endif()

  string(REPLACE "${_prefix}" "" _path "${_out}")
  string(STRIP "${_path}" _path)
  set(${out_var} "${_path}" PARENT_SCOPE)
endfunction()

set(_probe_root "${BUILD_ROOT}/producer_invalidation_probe")
set(_probe_source "${_probe_root}/source")
file(REMOVE_RECURSE "${_probe_root}")
file(MAKE_DIRECTORY
  "${_probe_source}/src"
  "${_probe_source}/cmake"
  "${_probe_source}/include/gentest"
  "${_probe_source}/tools/src")

file(WRITE "${_probe_source}/CMakeLists.txt" "cmake_minimum_required(VERSION 3.31)\nproject(${PACKAGE_NAME} LANGUAGES CXX)\n")
file(WRITE "${_probe_source}/src/CMakeLists.txt" "# probe\n")
file(WRITE "${_probe_source}/cmake/gentestConfig.cmake.in" "# probe\n")
file(WRITE "${_probe_source}/cmake/GentestCodegen.cmake" "# probe\n")
file(WRITE "${_probe_source}/include/gentest/runner.h" "// probe\n")
file(WRITE "${_probe_source}/tools/CMakeLists.txt" "# probe\n")
file(WRITE "${_probe_source}/tools/src/main.cpp" "// native codegen probe v1\n")

set(_toolchain_file "${_probe_root}/toolchain.cmake")
file(WRITE "${_toolchain_file}" "set(CMAKE_CXX_STANDARD 20)\n")
_gentest_capture_producer_dir("${_probe_source}" _toolchain_dir_before
  TOOLCHAIN_FILE "${_toolchain_file}"
  USE_MODULES OFF
  INJECT_CODEGEN ON
  LINK_MODE main_only)
file(WRITE "${_toolchain_file}" "set(CMAKE_CXX_STANDARD 23)\n")
_gentest_capture_producer_dir("${_probe_source}" _toolchain_dir_after
  TOOLCHAIN_FILE "${_toolchain_file}"
  USE_MODULES OFF
  INJECT_CODEGEN ON
  LINK_MODE main_only)

if(_toolchain_dir_before STREQUAL _toolchain_dir_after)
  message(FATAL_ERROR
    "Expected producer dir to change after editing the toolchain file in place, but both runs resolved to '${_toolchain_dir_before}'")
endif()

_gentest_capture_producer_dir("${_probe_source}" _native_dir_before
  USE_MODULES OFF
  INJECT_CODEGEN OFF
  LINK_MODE main_only)
file(WRITE "${_probe_source}/tools/src/main.cpp" "// native codegen probe v2\n")
_gentest_capture_producer_dir("${_probe_source}" _native_dir_after
  USE_MODULES OFF
  INJECT_CODEGEN OFF
  LINK_MODE main_only)

if(_native_dir_before STREQUAL _native_dir_after)
  message(FATAL_ERROR
    "Expected native-codegen producer dir to change after editing tools/**, but both runs resolved to '${_native_dir_before}'")
endif()

set(_fake_compiler "${_probe_root}/fake-compiler${CMAKE_EXECUTABLE_SUFFIX}")
file(COPY_FILE "${CMAKE_COMMAND}" "${_fake_compiler}")
if(NOT CMAKE_HOST_WIN32)
  file(CHMOD "${_fake_compiler}"
    PERMISSIONS
      OWNER_READ OWNER_WRITE OWNER_EXECUTE
      GROUP_READ GROUP_EXECUTE
      WORLD_READ WORLD_EXECUTE)
endif()
_gentest_capture_producer_dir("${_probe_source}" _compiler_dir_before
  USE_MODULES OFF
  INJECT_CODEGEN ON
  LINK_MODE main_only
  EXTRA_CACHE_ARGS
    "-DPACKAGE_TEST_C_COMPILER=${_fake_compiler}"
    "-DPACKAGE_TEST_CXX_COMPILER=${_fake_compiler}")
file(COPY_FILE "${CMAKE_CTEST_COMMAND}" "${_fake_compiler}" ONLY_IF_DIFFERENT)
if(NOT CMAKE_HOST_WIN32)
  file(CHMOD "${_fake_compiler}"
    PERMISSIONS
      OWNER_READ OWNER_WRITE OWNER_EXECUTE
      GROUP_READ GROUP_EXECUTE
      WORLD_READ WORLD_EXECUTE)
endif()
_gentest_capture_producer_dir("${_probe_source}" _compiler_dir_after
  USE_MODULES OFF
  INJECT_CODEGEN ON
  LINK_MODE main_only
  EXTRA_CACHE_ARGS
    "-DPACKAGE_TEST_C_COMPILER=${_fake_compiler}"
    "-DPACKAGE_TEST_CXX_COMPILER=${_fake_compiler}")

if(_compiler_dir_before STREQUAL _compiler_dir_after)
  message(FATAL_ERROR
    "Expected producer dir to change after replacing the compiler binary in place, but both runs resolved to '${_compiler_dir_before}'")
endif()

message(STATUS "Package consumer producer invalidation regression passed")
