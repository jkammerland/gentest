# Requires:
#  -DBUILD_ROOT=<path>
#  -DGENTEST_SOURCE_DIR=<path to gentest source tree>
#  -DPROG=<path to gentest_codegen>

if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenCompileCommandConditionWarnings.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenCompileCommandConditionWarnings.cmake: GENTEST_SOURCE_DIR not set")
endif()
if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenCompileCommandConditionWarnings.cmake: PROG not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckFixtureWriteHelpers.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

set(_work_dir "${BUILD_ROOT}/codegen_compile_command_condition_warnings")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

gentest_resolve_clang_fixture_compilers(_clang _clangxx)
if(NOT _clang OR NOT _clangxx)
  gentest_skip_test("compile-command condition warning regression: clang/clang++ not found")
  return()
endif()

set(_source "${_work_dir}/condition.cppm")
set(_object "${_work_dir}/condition.o")
set(_compdb "${_work_dir}/compile_commands.json")

gentest_fixture_write_file("${_source}" [=[
module;
#include "gentest/attributes.h"

#if GENTEST_SCAN_ENABLED == 0
import gentest.scan_compile_command_condition_warning_missing;
#endif

export module gentest.scan.compile_command_condition_warning;

export namespace gentest::scan::compile_command_condition_warning {

[[using gentest: test("scan/compile_command_condition_warning")]]
void condition_selected() {}

} // namespace gentest::scan::compile_command_condition_warning
]=])

set(_common_args
  "${_clangxx}"
  "-std=c++20"
  "-I${GENTEST_SOURCE_DIR}/include"
  "-I${GENTEST_SOURCE_DIR}/tests")
if(CMAKE_HOST_APPLE)
  set(_sdkroot "$ENV{SDKROOT}")
  if("${_sdkroot}" STREQUAL "")
    execute_process(
      COMMAND xcrun --sdk macosx --show-sdk-path
      RESULT_VARIABLE _xcrun_rc
      OUTPUT_VARIABLE _xcrun_out
      ERROR_VARIABLE _xcrun_err
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_STRIP_TRAILING_WHITESPACE)
    if(_xcrun_rc EQUAL 0 AND NOT "${_xcrun_out}" STREQUAL "")
      set(_sdkroot "${_xcrun_out}")
    endif()
  endif()
  if(NOT "${_sdkroot}" STREQUAL "")
    list(APPEND _common_args "-isysroot" "${_sdkroot}")
  endif()
endif()

gentest_fixture_make_compdb_entry(
  _entry
  DIRECTORY "${_work_dir}"
  FILE "${_source}"
  ARGUMENTS ${_common_args} "-DGENTEST_SCAN_ENABLED=1" "-c" "${_source}" "-o" "${_object}")
gentest_fixture_write_compdb("${_compdb}" "${_entry}")

execute_process(
  COMMAND
    "${PROG}"
    --check
    --compdb "${_work_dir}"
    --scan-deps-mode=OFF
    "${_source}"
  WORKING_DIRECTORY "${_work_dir}"
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

set(_all "${_out}\n${_err}")
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "Expected gentest_codegen --check to succeed. Output:\n${_all}")
endif()

string(FIND "${_all}" "warning: unable to evaluate preprocessor condition during module/import scan" _warn_pos)
if(NOT _warn_pos EQUAL -1)
  message(FATAL_ERROR
    "Expected compile-command-defined supported condition to avoid unknown-condition warnings. Output:\n${_all}")
endif()
