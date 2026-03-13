# Requires:
#  -DSOURCE_DIR=<path to gentest source tree>
#  -DBUILD_ROOT=<path to build/tests root>
#  -DPACKAGE_NAME=<name>

if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckPackageConsumerWorkdirIsolation.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckPackageConsumerWorkdirIsolation.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED PACKAGE_NAME OR "${PACKAGE_NAME}" STREQUAL "")
  message(FATAL_ERROR "CheckPackageConsumerWorkdirIsolation.cmake: PACKAGE_NAME not set")
endif()

function(_gentest_capture_package_work_dir inject_codegen out_var)
  execute_process(
    COMMAND
      "${CMAKE_COMMAND}"
      "-DSOURCE_DIR=${SOURCE_DIR}"
      "-DBUILD_ROOT=${BUILD_ROOT}"
      "-DPACKAGE_NAME=${PACKAGE_NAME}"
      "-DCONSUMER_LINK_MODE=main_only"
      "-DPACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE=${inject_codegen}"
      "-DPACKAGE_TEST_DRY_RUN_WORK_DIR=ON"
      "-DBUILD_TYPE=Debug"
      -P "${CMAKE_CURRENT_LIST_DIR}/CheckPackageConsumer.cmake"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)

  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR
      "Failed to compute package consumer work dir for inject=${inject_codegen}\n${_out}\n${_err}")
  endif()

  set(_prefix "-- CheckPackageConsumer work dir: ")
  string(FIND "${_out}" "${_prefix}" _prefix_pos)
  if(_prefix_pos EQUAL -1)
    message(FATAL_ERROR
      "Did not find package work dir marker for inject=${inject_codegen}\n${_out}\n${_err}")
  endif()
  string(REPLACE "${_prefix}" "" _path "${_out}")
  string(STRIP "${_path}" _path)
  set(${out_var} "${_path}" PARENT_SCOPE)
endfunction()

_gentest_capture_package_work_dir(ON _external_work_dir)
_gentest_capture_package_work_dir(OFF _native_work_dir)

if(_external_work_dir STREQUAL _native_work_dir)
  message(FATAL_ERROR
    "Expected package consumer external/native codegen work dirs to differ, but both resolved to '${_external_work_dir}'")
endif()

message(STATUS "Package consumer work dir isolation regression passed")
