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

#if defined(GENTEST_SCAN_DISABLED)
this branch must stay inactive when compile-command undef handling works
#elif GENTEST_SCAN_ENABLED == 0
import gentest.scan_compile_command_condition_warning_missing;
#endif

export module gentest.scan.compile_command_condition_warning;

export namespace gentest::scan::compile_command_condition_warning {

[[using gentest: test("scan/compile_command_condition_warning")]]
void condition_selected() {}

} // namespace gentest::scan::compile_command_condition_warning
]=])

gentest_make_public_api_include_args(
  _common_include_args
  SOURCE_ROOT "${GENTEST_SOURCE_DIR}"
  INCLUDE_TESTS
  APPLE_SYSROOT)
set(_common_args "${_clangxx}" "-std=c++20" ${_common_include_args})

gentest_fixture_make_compdb_entry(
  _entry
  DIRECTORY "${_work_dir}"
  FILE "${_source}"
  ARGUMENTS
    ${_common_args}
    "-DGENTEST_SCAN_ENABLED=1"
    "-DGENTEST_SCAN_DISABLED=1"
    "-UGENTEST_SCAN_DISABLED"
    "-c"
    "${_source}"
    "-o"
    "${_object}")
gentest_fixture_write_compdb("${_compdb}" "${_entry}")

function(_gentest_assert_condition_check_succeeds compdb_dir label)
  execute_process(
    COMMAND
      "${PROG}"
      --check
      --compdb "${compdb_dir}"
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
    message(FATAL_ERROR "Expected gentest_codegen --check to succeed for ${label}. Output:\n${_all}")
  endif()

  string(FIND "${_all}" "warning: unable to evaluate preprocessor condition during module/import scan" _warn_pos)
  if(NOT _warn_pos EQUAL -1)
    message(FATAL_ERROR
      "Expected compile-command-defined supported condition to avoid unknown-condition warnings for ${label}. Output:\n${_all}")
  endif()
endfunction()

_gentest_assert_condition_check_succeeds("${_work_dir}" "GNU-style -D/-U compile-command macros")

find_program(_gxx NAMES g++ c++)
if(_gxx)
  set(_gcc_modules_compdb_dir "${_work_dir}/gcc_modules")
  set(_gcc_modules_object "${_work_dir}/condition_gcc_modules.o")
  file(MAKE_DIRECTORY "${_gcc_modules_compdb_dir}")
  gentest_fixture_make_compdb_entry(
    _gcc_modules_entry
    DIRECTORY "${_work_dir}"
    FILE "${_source}"
    ARGUMENTS
      "${_gxx}"
      "-std=c++20"
      "-fmodules-ts"
      "-fmodule-only"
      ${_common_include_args}
      "-DGENTEST_SCAN_ENABLED=1"
      "-DGENTEST_SCAN_DISABLED=1"
      "-UGENTEST_SCAN_DISABLED"
      "-c"
      "${_source}"
      "-o"
      "${_gcc_modules_object}")
  gentest_fixture_write_compdb("${_gcc_modules_compdb_dir}/compile_commands.json" "${_gcc_modules_entry}")
  _gentest_assert_condition_check_succeeds(
    "${_gcc_modules_compdb_dir}"
    "GCC-style module compile flags (-fmodules-ts -fmodule-only)")
endif()

if(CMAKE_HOST_WIN32)
  get_filename_component(_clang_dir "${_clangxx}" DIRECTORY)
  set(_clang_cl "${_clang_dir}/clang-cl.exe")
  if(NOT EXISTS "${_clang_cl}")
    gentest_skip_test("compile-command condition warning regression: clang-cl.exe not found next to '${_clangxx}'")
    return()
  endif()
  set(_msvc_compdb_dir "${_work_dir}/msvc_style")
  set(_msvc_object "${_work_dir}/condition_msvc.obj")
  file(MAKE_DIRECTORY "${_msvc_compdb_dir}")
  gentest_fixture_make_compdb_entry(
    _msvc_entry
    DIRECTORY "${_work_dir}"
    FILE "${_source}"
    ARGUMENTS
      "${_clang_cl}"
      "/std:c++20"
      "/I${GENTEST_SOURCE_DIR}/include"
      "/I${GENTEST_SOURCE_DIR}/tests"
      "/D"
      "GENTEST_SCAN_ENABLED=1"
      "/D"
      "GENTEST_SCAN_DISABLED=1"
      "/U"
      "GENTEST_SCAN_DISABLED"
      "/c"
      "${_source}"
      "/Fo${_msvc_object}")
  gentest_fixture_write_compdb("${_msvc_compdb_dir}/compile_commands.json" "${_msvc_entry}")
  _gentest_assert_condition_check_succeeds("${_msvc_compdb_dir}" "Windows-style /D and /U compile-command macros under clang-cl")
endif()
