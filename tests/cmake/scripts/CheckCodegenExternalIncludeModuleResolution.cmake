# Requires:
#  -DBUILD_ROOT=<path>
#  -DGENTEST_SOURCE_DIR=<path to gentest source tree>
#  -DPROG=<path to gentest_codegen>

if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenExternalIncludeModuleResolution.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenExternalIncludeModuleResolution.cmake: GENTEST_SOURCE_DIR not set")
endif()
if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenExternalIncludeModuleResolution.cmake: PROG not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckFixtureWriteHelpers.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

if(NOT CMAKE_HOST_WIN32)
  gentest_skip_test("external include module resolution regression: requires Windows host")
  return()
endif()

set(_work_dir "${BUILD_ROOT}/codegen_external_include_module_resolution")
set(_external_dir "${_work_dir}/external/gentest")
set(_external_module "${_external_dir}/gentest.cppm")

file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_external_dir}")

gentest_resolve_clang_fixture_compilers(_clang _clangxx)
if(NOT _clang OR NOT _clangxx)
  gentest_skip_test("external include module resolution regression: clang/clang++ not found")
  return()
endif()

get_filename_component(_clang_dir "${_clangxx}" DIRECTORY)
set(_clang_cl "${_clang_dir}/clang-cl.exe")
if(NOT EXISTS "${_clang_cl}")
  gentest_skip_test("external include module resolution regression: clang-cl.exe not found next to '${_clangxx}'")
  return()
endif()

gentest_find_clang_scan_deps(_clang_scan_deps "${_clangxx}")
if("${_clang_scan_deps}" STREQUAL "")
  gentest_skip_test("external include module resolution regression: clang-scan-deps not found")
  return()
endif()

gentest_fixture_write_file("${_external_module}" [=[
export module gentest;

export namespace gentest {
inline auto imported_marker() -> int { return 42; }
} // namespace gentest
]=])

function(_gentest_run_external_include_case case_name include_flag scan_mode expect_scan_deps)
  set(_case_dir "${_work_dir}/${case_name}")
  set(_generated_dir "${_case_dir}/generated")
  set(_consumer "${_case_dir}/consumer.cppm")
  set(_consumer_wrapper "${_generated_dir}/tu_0000_consumer.module.gentest.cppm")
  set(_consumer_obj "${_case_dir}/consumer.obj")
  set(_compdb "${_case_dir}/compile_commands.json")
  file(MAKE_DIRECTORY "${_case_dir}" "${_generated_dir}")

  set(_consumer_source [=[
export module gentest.external.consumer;

import gentest;

export namespace gentest::external::consumer {

[[using gentest: test("scan/external_include_module_resolution/@CASE@")]]
void resolves_public_module_import() {
  (void)gentest::imported_marker();
}

} // namespace gentest::external::consumer
]=])
  string(REPLACE "@CASE@" "${case_name}" _consumer_source "${_consumer_source}")
  gentest_fixture_write_file("${_consumer}" "${_consumer_source}")

  gentest_fixture_make_compdb_entry(
    _entry
    DIRECTORY "${_case_dir}"
    FILE "${_consumer}"
    ARGUMENTS
      "${_clang_cl}"
      "/std:c++20"
      "${include_flag}"
      "/c"
      "${_consumer}"
      "/Fo${_consumer_obj}")
  gentest_fixture_write_compdb("${_compdb}" "${_entry}")

  set(_command
    "${PROG}"
    --compdb "${_case_dir}"
    --clang-scan-deps "${_clang_scan_deps}"
    --tu-out-dir "${_generated_dir}"
    --module-wrapper-output "${_consumer_wrapper}"
    --tu-header-output "${_generated_dir}/consumer.gentest.h"
    "${_consumer}")
  if(NOT "${scan_mode}" STREQUAL "AUTO")
    list(APPEND _command "--scan-deps-mode=${scan_mode}")
  endif()

  message(STATUS "Run gentest_codegen for ${case_name}...")
  gentest_check_run_or_fail(
    COMMAND
      "${CMAKE_COMMAND}"
      -E
      env
      GENTEST_CODEGEN_LOG_SCAN_DEPS=1
      ${_command}
    OUTPUT_VARIABLE _output
    WORKING_DIRECTORY "${_case_dir}"
    STRIP_TRAILING_WHITESPACE)

  if(NOT EXISTS "${_generated_dir}/consumer.gentest.h")
    message(FATAL_ERROR "Expected consumer registration header to be generated for ${case_name}")
  endif()

  file(READ "${_generated_dir}/consumer.gentest.h" _header_text)
  string(FIND "${_header_text}" "resolves_public_module_import" _header_pos)
  if(_header_pos EQUAL -1)
    message(FATAL_ERROR "Expected generated header for ${case_name} to contain the imported-module test registration")
  endif()

  if(expect_scan_deps)
    string(FIND "${_output}" "using clang-scan-deps for named-module dependency discovery" _scan_deps_pos)
    if(_scan_deps_pos EQUAL -1)
      message(FATAL_ERROR
        "Expected default scan-deps AUTO path to use clang-scan-deps for ${case_name}. Output:\n${_output}")
    endif()
  endif()
endfunction()

_gentest_run_external_include_case(
  slash_auto
  "/external:I${_work_dir}/external"
  "AUTO"
  TRUE)

_gentest_run_external_include_case(
  dash_off
  "-external:I${_work_dir}/external"
  "OFF"
  FALSE)
