# Requires:
#  -DSOURCE_DIR=<path to gentest source tree>
#  -DBUILD_ROOT=<path to build/tests root>
#  -DPACKAGE_NAME=<name>

if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckPackageConsumerExecutablePath.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckPackageConsumerExecutablePath.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED PACKAGE_NAME OR "${PACKAGE_NAME}" STREQUAL "")
  message(FATAL_ERROR "CheckPackageConsumerExecutablePath.cmake: PACKAGE_NAME not set")
endif()

function(_gentest_capture_consumer_exe generator build_config out_var)
  set(_compiler_args)
  if(DEFINED C_COMPILER AND NOT "${C_COMPILER}" STREQUAL "")
    list(APPEND _compiler_args "-DPACKAGE_TEST_C_COMPILER=${C_COMPILER}")
  endif()
  if(DEFINED CXX_COMPILER AND NOT "${CXX_COMPILER}" STREQUAL "")
    list(APPEND _compiler_args "-DPACKAGE_TEST_CXX_COMPILER=${CXX_COMPILER}")
  endif()
  if(DEFINED PROG AND NOT "${PROG}" STREQUAL "")
    list(APPEND _compiler_args "-DPROG=${PROG}")
  endif()

  execute_process(
    COMMAND
      "${CMAKE_COMMAND}"
      "-DSOURCE_DIR=${SOURCE_DIR}"
      "-DBUILD_ROOT=${BUILD_ROOT}"
      "-DPACKAGE_NAME=${PACKAGE_NAME}"
      "-DGENERATOR=${generator}"
      "-DCONSUMER_LINK_MODE=double"
      "-DPACKAGE_TEST_USE_MODULES=ON"
      "-DPACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE=ON"
      "-DPACKAGE_TEST_DRY_RUN_CONSUMER_EXE=ON"
      "-DBUILD_TYPE=Debug"
      "-DBUILD_CONFIG=${build_config}"
      ${_compiler_args}
      -P "${CMAKE_CURRENT_LIST_DIR}/CheckPackageConsumer.cmake"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)

  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR
      "Failed to compute package consumer executable path for generator='${generator}' config='${build_config}'\n${_out}\n${_err}")
  endif()

  set(_prefix "-- CheckPackageConsumer consumer exe: ")
  string(FIND "${_out}" "${_prefix}" _prefix_pos)
  if(_prefix_pos EQUAL -1)
    message(FATAL_ERROR
      "Did not find consumer executable marker for generator='${generator}' config='${build_config}'\n${_out}\n${_err}")
  endif()
  string(REPLACE "${_prefix}" "" _path "${_out}")
  string(STRIP "${_path}" _path)
  set(${out_var} "${_path}" PARENT_SCOPE)
endfunction()

set(_exe_suffix "")
if(CMAKE_HOST_WIN32)
  set(_exe_suffix ".exe")
endif()

_gentest_capture_consumer_exe("Ninja" "Release" _single_config_exe)
if(_single_config_exe MATCHES "[/\\\\]Release[/\\\\]gentest_consumer${_exe_suffix}$")
  message(FATAL_ERROR
    "Expected single-config generator consumer path to point directly at the build dir executable, got '${_single_config_exe}'")
endif()
if(NOT _single_config_exe MATCHES "gentest_consumer${_exe_suffix}$")
  message(FATAL_ERROR
    "Expected single-config generator consumer path to end with gentest_consumer${_exe_suffix}, got '${_single_config_exe}'")
endif()

_gentest_capture_consumer_exe("Ninja Multi-Config" "Release" _multi_config_exe)
if(NOT _multi_config_exe MATCHES "[/\\\\]Release[/\\\\]gentest_consumer${_exe_suffix}$")
  message(FATAL_ERROR
    "Expected multi-config generator consumer path to include the selected config directory, got '${_multi_config_exe}'")
endif()

message(STATUS "Package consumer executable path regression passed")
