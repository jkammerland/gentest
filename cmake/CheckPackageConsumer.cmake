# Usage:
#   cmake
#     -DSOURCE_DIR=<path>
#     -DBUILD_ROOT=<path>
#     -DPACKAGE_NAME=<name>
#     [-DGENERATOR=<cmake generator>]
#     [-DGENERATOR_PLATFORM=<platform>]
#     [-DGENERATOR_TOOLSET=<toolset>]
#     [-DTOOLCHAIN_FILE=<toolchain.cmake>]
#     [-DMAKE_PROGRAM=<path>]
#     [-DC_COMPILER=<path>]
#     [-DCXX_COMPILER=<path>]
#     [-DBUILD_TYPE=<Debug|Release|...>]
#     [-DBUILD_CONFIG=<Debug|Release|...>]   # for multi-config generators
#     -P cmake/CheckPackageConsumer.cmake

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT)
  message(FATAL_ERROR "BUILD_ROOT not set")
endif()
if(NOT DEFINED PACKAGE_NAME)
  message(FATAL_ERROR "PACKAGE_NAME not set")
endif()

function(run_or_fail)
  set(options "")
  set(oneValueArgs WORKING_DIRECTORY)
  set(multiValueArgs COMMAND)
  cmake_parse_arguments(RUN "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT RUN_COMMAND)
    message(FATAL_ERROR "run_or_fail: COMMAND is required")
  endif()

  execute_process(
    COMMAND ${RUN_COMMAND}
    WORKING_DIRECTORY "${RUN_WORKING_DIRECTORY}"
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    RESULT_VARIABLE rc)

  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "Command failed (${rc}): ${RUN_COMMAND}\n--- stdout ---\n${out}\n--- stderr ---\n${err}\n")
  endif()
endfunction()

set(_work_dir "${BUILD_ROOT}/package_consumer")
set(_producer_build_dir "${_work_dir}/producer")
set(_install_prefix "${_work_dir}/install")
set(_consumer_build_dir "${_work_dir}/consumer")
set(_consumer_source_dir "${SOURCE_DIR}/tests/consumer")

file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

set(_cmake_generator_args)
if(DEFINED GENERATOR AND NOT GENERATOR STREQUAL "")
  list(APPEND _cmake_generator_args -G "${GENERATOR}")
  if(DEFINED GENERATOR_PLATFORM AND NOT GENERATOR_PLATFORM STREQUAL "")
    list(APPEND _cmake_generator_args -A "${GENERATOR_PLATFORM}")
  endif()
  if(DEFINED GENERATOR_TOOLSET AND NOT GENERATOR_TOOLSET STREQUAL "")
    list(APPEND _cmake_generator_args -T "${GENERATOR_TOOLSET}")
  endif()
endif()

set(_cmake_cache_args)
if(DEFINED TOOLCHAIN_FILE AND NOT TOOLCHAIN_FILE STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}")
endif()
if(DEFINED MAKE_PROGRAM AND NOT MAKE_PROGRAM STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()
if(DEFINED C_COMPILER AND NOT C_COMPILER STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_C_COMPILER=${C_COMPILER}")
endif()
if(DEFINED CXX_COMPILER AND NOT CXX_COMPILER STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}")
endif()
if(DEFINED BUILD_TYPE AND NOT BUILD_TYPE STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()

message(STATUS "Configure producer build (${PACKAGE_NAME})...")
run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_generator_args}
    -S "${SOURCE_DIR}"
    -B "${_producer_build_dir}"
    ${_cmake_cache_args}
    "-D${PACKAGE_NAME}_INSTALL=ON"
    "-D${PACKAGE_NAME}_BUILD_TESTING=OFF"
    "-DCMAKE_INSTALL_PREFIX=${_install_prefix}")

message(STATUS "Build and install producer into '${_install_prefix}'...")
set(_producer_build_args --build "${_producer_build_dir}" --target install)
if(DEFINED BUILD_CONFIG AND NOT BUILD_CONFIG STREQUAL "")
  list(APPEND _producer_build_args --config "${BUILD_CONFIG}")
endif()
run_or_fail(COMMAND "${CMAKE_COMMAND}" ${_producer_build_args})

message(STATUS "Locate installed '${PACKAGE_NAME}' CMake package...")
file(GLOB_RECURSE _config_candidates
  LIST_DIRECTORIES FALSE
  "${_install_prefix}/*/${PACKAGE_NAME}Config.cmake"
  "${_install_prefix}/*/*/${PACKAGE_NAME}Config.cmake")
if(NOT _config_candidates)
  message(FATAL_ERROR "Installed package config '${PACKAGE_NAME}Config.cmake' not found under '${_install_prefix}'")
endif()
list(GET _config_candidates 0 _config_file)
get_filename_component(_config_dir "${_config_file}" DIRECTORY)

message(STATUS "Configure consumer project...")
if(NOT EXISTS "${_consumer_source_dir}/CMakeLists.txt")
  message(FATAL_ERROR "Consumer project not found: '${_consumer_source_dir}'")
endif()

set(_consumer_dir_var "${PACKAGE_NAME}_DIR")
run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_generator_args}
    -S "${_consumer_source_dir}"
    -B "${_consumer_build_dir}"
    ${_cmake_cache_args}
    "-D${_consumer_dir_var}=${_config_dir}")

message(STATUS "Build consumer project...")
set(_consumer_build_args --build "${_consumer_build_dir}")
if(DEFINED BUILD_CONFIG AND NOT BUILD_CONFIG STREQUAL "")
  list(APPEND _consumer_build_args --config "${BUILD_CONFIG}")
endif()
run_or_fail(COMMAND "${CMAKE_COMMAND}" ${_consumer_build_args})

set(_exe_ext "")
if(CMAKE_HOST_WIN32)
  set(_exe_ext ".exe")
endif()

set(_consumer_exe "${_consumer_build_dir}/gentest_consumer${_exe_ext}")
if(DEFINED BUILD_CONFIG AND NOT BUILD_CONFIG STREQUAL "")
  set(_consumer_exe "${_consumer_build_dir}/${BUILD_CONFIG}/gentest_consumer${_exe_ext}")
endif()
if(NOT EXISTS "${_consumer_exe}")
  message(FATAL_ERROR "Consumer executable not found: '${_consumer_exe}'")
endif()

message(STATUS "Run consumer executable...")
run_or_fail(COMMAND "${_consumer_exe}")

