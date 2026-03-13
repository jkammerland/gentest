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

function(_gentest_capture_package_work_dir use_modules inject_codegen build_config out_var)
  set(_compiler_args)
  if(DEFINED C_COMPILER AND NOT "${C_COMPILER}" STREQUAL "")
    list(APPEND _compiler_args "-DPACKAGE_TEST_C_COMPILER=${C_COMPILER}")
  endif()
  if(DEFINED CXX_COMPILER AND NOT "${CXX_COMPILER}" STREQUAL "")
    list(APPEND _compiler_args "-DPACKAGE_TEST_CXX_COMPILER=${CXX_COMPILER}")
  endif()
  execute_process(
    COMMAND
      "${CMAKE_COMMAND}"
      "-DSOURCE_DIR=${SOURCE_DIR}"
      "-DBUILD_ROOT=${BUILD_ROOT}"
      "-DPACKAGE_NAME=${PACKAGE_NAME}"
      "-DCONSUMER_LINK_MODE=main_only"
      "-DPACKAGE_TEST_USE_MODULES=${use_modules}"
      "-DPACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE=${inject_codegen}"
      "-DPACKAGE_TEST_DRY_RUN_WORK_DIR=ON"
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
      "Failed to compute package consumer work dir for use_modules=${use_modules} inject=${inject_codegen} config=${build_config}\n${_out}\n${_err}")
  endif()

  set(_prefix "-- CheckPackageConsumer work dir: ")
  string(FIND "${_out}" "${_prefix}" _prefix_pos)
  if(_prefix_pos EQUAL -1)
    message(FATAL_ERROR
      "Did not find package work dir marker for use_modules=${use_modules} inject=${inject_codegen} config=${build_config}\n${_out}\n${_err}")
  endif()
  string(REPLACE "${_prefix}" "" _path "${_out}")
  string(STRIP "${_path}" _path)
  set(${out_var} "${_path}" PARENT_SCOPE)
endfunction()

set(_build_config "Debug")
_gentest_capture_package_work_dir(ON ON "${_build_config}" _module_external_work_dir)
_gentest_capture_package_work_dir(ON OFF "${_build_config}" _module_native_work_dir)
_gentest_capture_package_work_dir(OFF ON "${_build_config}" _include_external_work_dir)
_gentest_capture_package_work_dir(OFF OFF "${_build_config}" _include_native_work_dir)

if(_module_external_work_dir STREQUAL _module_native_work_dir)
  message(FATAL_ERROR
    "Expected module package consumer external/native codegen work dirs to differ, but both resolved to '${_module_external_work_dir}'")
endif()

if(_module_external_work_dir STREQUAL _include_external_work_dir)
  message(FATAL_ERROR
    "Expected module/include package consumer work dirs to differ, but both resolved to '${_module_external_work_dir}'")
endif()

if(_include_external_work_dir STREQUAL _include_native_work_dir)
  message(FATAL_ERROR
    "Expected include-only package consumer external/native codegen work dirs to differ, but both resolved to '${_include_external_work_dir}'")
endif()

message(STATUS "Package consumer work dir isolation regression passed")
