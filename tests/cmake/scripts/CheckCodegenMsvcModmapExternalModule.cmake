# Requires:
#  -DBUILD_ROOT=<path>
#  -DGENTEST_SOURCE_DIR=<path to gentest source tree>
#  -DPROG=<path to gentest_codegen>
#
# Regression test for the MSVC .modmap flag stripping in gentest_codegen.
#
# CMake's MSVC C++-modules support exports compile_commands.json entries that
# reference a ".modmap" response file carrying cl.exe module flags
# ("-interface", "-ifcOutput <bmi>", "-reference <name>=<bmi>", ...). When
# gentest_codegen resolves a named module that is absent from the compilation
# database, LLVM's InterpolatingCompilationDatabase borrows the nearest
# command and drops "-ifcOutput"'s value (an OPT_INPUT argument) while keeping
# the following "-reference" flag. A strip loop that unconditionally consumes
# the token after "-ifcOutput" then swallows the "-reference" flag and leaves
# its "<name>=<bmi>" value as a stray positional source, which fails the
# external-module scan with "no such file or directory".
#
# This test reproduces that setup (cl driver-mode command + @modmap + external
# module absent from the compdb) and requires codegen to succeed, for both a
# relative and an absolute "-ifcOutput" value: the value-aware strip must
# consume an absolute POSIX path as a value instead of misclassifying it as an
# option and leaking it as a stray positional.

if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenMsvcModmapExternalModule.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenMsvcModmapExternalModule.cmake: GENTEST_SOURCE_DIR not set")
endif()
if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenMsvcModmapExternalModule.cmake: PROG not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckFixtureWriteHelpers.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

gentest_resolve_clang_fixture_compilers(_clang _clangxx)
if(NOT _clang OR NOT _clangxx)
  gentest_skip_test("msvc modmap external module regression: clang/clang++ not found")
  return()
endif()

execute_process(
  COMMAND "${_clangxx}" --driver-mode=cl --version
  RESULT_VARIABLE _driver_mode_rc
  OUTPUT_QUIET
  ERROR_QUIET)
if(NOT _driver_mode_rc EQUAL 0)
  gentest_skip_test("msvc modmap external module regression: '${_clangxx}' does not support --driver-mode=cl")
  return()
endif()

gentest_find_clang_scan_deps(_clang_scan_deps "${_clangxx}")
if("${_clang_scan_deps}" STREQUAL "")
  gentest_skip_test("msvc modmap external module regression: clang-scan-deps not found")
  return()
endif()

set(_work_dir "${BUILD_ROOT}/codegen_msvc_modmap_external_module")
set(_external_dir "${_work_dir}/external/gentest")
set(_external_module "${_external_dir}/gentest.cppm")

file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_external_dir}")

gentest_fixture_write_file("${_external_module}" [=[
module;

export module gentest;

export namespace gentest {
inline auto gentest_version() -> int { return 1; }
} // namespace gentest
]=])

set(_variants "relative" "absolute")
foreach(_variant IN LISTS _variants)
  set(_case_dir "${_work_dir}/case_${_variant}")
  set(_generated_dir "${_case_dir}/generated")
  set(_consumer "${_case_dir}/consumer.cppm")
  set(_modmap_dir "${_case_dir}/CMakeFiles/gentest_consumer.dir")
  set(_modmap "${_modmap_dir}/cases.cppm.modmap")
  set(_compdb "${_case_dir}/compile_commands.json")

  file(MAKE_DIRECTORY "${_generated_dir}" "${_modmap_dir}")

  if(_variant STREQUAL "absolute")
    set(_ifc_output_value "\"${_case_dir}/CMakeFiles/gentest_consumer.dir/cases.cppm.bmi\"")
  else()
    set(_ifc_output_value "\"CMakeFiles/gentest_consumer.dir/cases.cppm.bmi\"")
  endif()

  file(WRITE "${_modmap}" "-interface
-ifcOutput ${_ifc_output_value}
-reference \"gentest=CMakeFiles/gentest__gentest@synth_x.dir/abc123.bmi\"
")

  gentest_fixture_write_file("${_consumer}" [=[
module;

export module gentest.consumer_cases;

import gentest;

export namespace gentest::consumer_cases {

[[using gentest: test("scan/msvc_modmap_external_module")]]
void resolves_external_module_through_modmap() {
  (void)gentest::gentest_version();
}

} // namespace gentest::consumer_cases
]=])

  gentest_fixture_make_compdb_entry(
    _entry
    DIRECTORY "${_case_dir}"
    FILE "${_consumer}"
    ARGUMENTS
      "cl"
      "/std:c++20"
      "/I${_external_dir}"
      "@CMakeFiles/gentest_consumer.dir/cases.cppm.modmap"
      "${_consumer}")
  gentest_fixture_write_compdb("${_compdb}" "${_entry}")

  set(_command
    "${PROG}"
    --compdb "${_case_dir}"
    --clang-scan-deps "${_clang_scan_deps}"
    --host-clang "${_clangxx}"
    --tu-out-dir "${_generated_dir}"
    --artifact-manifest "${_generated_dir}/artifact_manifest.json"
    --tu-header-output "${_generated_dir}/consumer.gentest.h"
    --module-registration-output "${_generated_dir}/tu_0000_consumer.registration.gentest.cpp"
    --source-root "${_case_dir}"
    "${_consumer}"
    --
    "-DGENTEST_CODEGEN=1"
    "-Wno-unknown-attributes"
    "-Wno-attributes"
    "-Wno-unknown-warning-option"
    "/std:c++20")
  message(STATUS "Run gentest_codegen for msvc modmap external module regression (${_variant})...")
  gentest_check_run_or_fail(
    COMMAND
      "${CMAKE_COMMAND}"
      -E
      env
      GENTEST_CODEGEN_LOG_SCAN_DEPS=1
      GENTEST_CODEGEN_LOG_PARSE_COMMANDS=1
      ${_command}
    OUTPUT_VARIABLE _output
    WORKING_DIRECTORY "${_case_dir}"
    STRIP_TRAILING_WHITESPACE)

  if(NOT EXISTS "${_generated_dir}/consumer.gentest.h")
    message(FATAL_ERROR "Expected consumer registration header to be generated (${_variant})")
  endif()

  file(READ "${_generated_dir}/consumer.gentest.h" _header_text)
  string(FIND "${_header_text}" "resolves_external_module_through_modmap" _header_pos)
  if(_header_pos EQUAL -1)
    message(FATAL_ERROR "Expected generated header to contain the imported-module test registration (${_variant})")
  endif()

  if(NOT EXISTS "${_generated_dir}/tu_0000_consumer.registration.gentest.cpp")
    message(FATAL_ERROR "Expected module registration translation unit to be generated (${_variant})")
  endif()

  if(NOT EXISTS "${_generated_dir}/artifact_manifest.json")
    message(FATAL_ERROR "Expected artifact manifest to be generated (${_variant})")
  endif()

  string(FIND "${_output}" "using clang-scan-deps for named-module dependency discovery" _scan_deps_pos)
  if(_scan_deps_pos EQUAL -1)
    message(FATAL_ERROR
      "Expected named-module codegen to use clang-scan-deps (${_variant}). Output:\n${_output}")
  endif()

  string(FIND "${_output}" "no such file or directory" _symptom_pos)
  if(NOT _symptom_pos EQUAL -1)
    message(FATAL_ERROR
      "Expected no 'no such file or directory' symptom in codegen output (${_variant}). Output:\n${_output}")
  endif()

  string(FIND "${_output}" "no input files" _no_input_pos)
  if(NOT _no_input_pos EQUAL -1)
    message(FATAL_ERROR
      "Expected no 'no input files' symptom in codegen output (${_variant}). Output:\n${_output}")
  endif()

  # Assert the shape of the parse command rather than relying on the host build
  # path. In MSVC driver mode the cl option parser consumes an absolute POSIX
  # input path as an option -- "/Users/..." matches "/U" (with a warning),
  # "/Include/..." matches "/I" and "/Development/..." matches "/D" with no
  # diagnostic at all -- so this test only fails on hosts whose build root
  # happens to collide. Requiring the input to be handed over behind "--" keeps
  # the guard covered everywhere. Windows paths start with a drive letter and
  # cannot collide, so the separator is neither emitted nor required there.
  if(_consumer MATCHES "^/")
    string(FIND "${_output}" "\n  --\n  ${_consumer}" _separator_pos)
    if(_separator_pos EQUAL -1)
      message(FATAL_ERROR
        "Expected the parse command to pass the input file behind '--' (${_variant}). Output:\n${_output}")
    endif()
  endif()
endforeach()
