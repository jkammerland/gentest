# Requires:
#  -DBUILD_ROOT=<path>
#  -DGENTEST_SOURCE_DIR=<path to gentest source tree>
#  -DPROG=<path to gentest_codegen>
#
# Regression test for the separate-value module flag stripping in gentest_codegen.
#
# Build systems emit GNU-style module and dependency-scanning flags with their
# value as a separate argument ("-fdeps-file <ddi>", "-fmodule-file
# <name>=<path>", ...). Clang's option table does not know any of them in that
# spelling: the bare flag is an unknown argument and its value is classified as
# an input. When gentest_codegen resolves a named module that is absent from the
# compilation database, LLVM's InterpolatingCompilationDatabase borrows the
# nearest command and drops every such value while keeping the flags, so
#
#   -fdeps-file <ddi> -include <prelude> <consumer>
#
# becomes
#
#   -fdeps-file -include <prelude> -- <external module>
#
# A strip loop that unconditionally consumes the token after "-fdeps-file" then
# swallows the "-include" flag and leaves <prelude> as a stray positional source.
#
# The variants cover the swallow itself, an orphaned flag at the very end of the
# argument list (the guard must not read past the end), and a value that starts
# with '/' (an ordinary absolute POSIX path, which must still be consumed as a
# value rather than mistaken for an option).

if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenSeparateModuleFlagStrip.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenSeparateModuleFlagStrip.cmake: GENTEST_SOURCE_DIR not set")
endif()
if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenSeparateModuleFlagStrip.cmake: PROG not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckFixtureWriteHelpers.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

gentest_resolve_clang_fixture_compilers(_clang _clangxx)
if(NOT _clang OR NOT _clangxx)
  gentest_skip_test("separate-value module flag strip: clang/clang++ not found")
  return()
endif()

gentest_find_clang_scan_deps(_clang_scan_deps "${_clangxx}")
if("${_clang_scan_deps}" STREQUAL "")
  gentest_skip_test("separate-value module flag strip: clang-scan-deps not found")
  return()
endif()

set(_work_dir "${BUILD_ROOT}/codegen_separate_module_flag_strip")
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

# swallow      -- an orphaned flag directly in front of a real separate-value
#                 option ("-include"), whose value leaks as a stray positional
#                 when the orphan consumes the flag. This is the variant that
#                 fails against an unconditional skip-next.
# trailing     -- an orphaned flag as the final argument of the compile command,
#                 so the lookahead has no token to inspect. An unconditional
#                 skip-next survives this one; it is here to pin the bounds
#                 check, which an unguarded index would turn into an
#                 out-of-range read.
# posix_value  -- a value that starts with '/', which must still be consumed.
#                 This guards the opposite mistake: reusing the slash-aware
#                 MSVC predicate here leaks the path as a stray positional,
#                 which is what this variant fails on.
set(_variants "swallow" "trailing" "posix_value")
foreach(_variant IN LISTS _variants)
  set(_case_dir "${_work_dir}/case_${_variant}")
  set(_generated_dir "${_case_dir}/generated")
  set(_consumer "${_case_dir}/consumer.cppm")
  set(_prelude "${_case_dir}/prelude.hpp")
  set(_compdb "${_case_dir}/compile_commands.json")

  file(MAKE_DIRECTORY "${_generated_dir}")

  gentest_fixture_write_file("${_prelude}" [=[
#pragma once
]=])

  gentest_fixture_write_file("${_consumer}" [=[
module;

export module gentest.consumer_cases;

import gentest;

export namespace gentest::consumer_cases {

[[using gentest: test("scan/separate_value_module_flag")]]
void resolves_external_module_with_orphaned_flags() {
  (void)gentest::gentest_version();
}

} // namespace gentest::consumer_cases
]=])

  if(_variant STREQUAL "swallow")
    gentest_fixture_make_compdb_entry(
      _entry
      DIRECTORY "${_case_dir}"
      FILE "${_consumer}"
      ARGUMENTS
        "clang++"
        "-std=c++20"
        "-I${_external_dir}"
        "-fdeps-file"
        "${_case_dir}/consumer.ddi"
        "-include"
        "${_prelude}"
        "${_consumer}")
  elseif(_variant STREQUAL "trailing")
    gentest_fixture_make_compdb_entry(
      _entry
      DIRECTORY "${_case_dir}"
      FILE "${_consumer}"
      ARGUMENTS
        "clang++"
        "-std=c++20"
        "-I${_external_dir}"
        "${_consumer}"
        "-fdeps-target")
  else()
    gentest_fixture_make_compdb_entry(
      _entry
      DIRECTORY "${_case_dir}"
      FILE "${_consumer}"
      ARGUMENTS
        "clang++"
        "-std=c++20"
        "-I${_external_dir}"
        "-fdeps-file"
        "${_case_dir}/absent/consumer.ddi"
        "${_consumer}")
  endif()
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
    "-Wno-unknown-warning-option")
  message(STATUS "Run gentest_codegen for separate-value module flag regression (${_variant})...")
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
  string(FIND "${_header_text}" "resolves_external_module_with_orphaned_flags" _header_pos)
  if(_header_pos EQUAL -1)
    message(FATAL_ERROR "Expected generated header to contain the imported-module test registration (${_variant})")
  endif()

  if(NOT EXISTS "${_generated_dir}/tu_0000_consumer.registration.gentest.cpp")
    message(FATAL_ERROR "Expected module registration translation unit to be generated (${_variant})")
  endif()

  if(NOT EXISTS "${_generated_dir}/artifact_manifest.json")
    message(FATAL_ERROR "Expected artifact manifest to be generated (${_variant})")
  endif()

  string(FIND "${_output}" "no such file or directory" _symptom_pos)
  if(NOT _symptom_pos EQUAL -1)
    message(FATAL_ERROR
      "Expected no 'no such file or directory' symptom in codegen output (${_variant}). Output:\n${_output}")
  endif()

  string(FIND "${_output}" "expected exactly one compiler job" _job_pos)
  if(NOT _job_pos EQUAL -1)
    message(FATAL_ERROR
      "Expected no stray positional source in codegen output (${_variant}). Output:\n${_output}")
  endif()

  # Assert the shape of the parse command, not only the outcome: none of the
  # separate-value flags may survive into it, in either spelling. A surviving
  # bare flag is an unknown argument to clang, and a surviving value is a stray
  # positional source.
  foreach(_flag "-fmodule-mapper" "-fdeps-format" "-fdeps-file" "-fdeps-target" "-fconcepts-diagnostics-depth")
    string(FIND "${_output}" "\n  ${_flag}\n" _flag_pos)
    if(NOT _flag_pos EQUAL -1)
      message(FATAL_ERROR
        "Expected '${_flag}' to be stripped from the parse command (${_variant}). Output:\n${_output}")
    endif()
  endforeach()

  if(_variant STREQUAL "swallow")
    # The "-include" flag the orphan used to swallow must survive with its value.
    string(FIND "${_output}" "\n  -include\n  ${_prelude}\n" _include_pos)
    if(_include_pos EQUAL -1)
      message(FATAL_ERROR
        "Expected '-include ${_prelude}' to survive in the parse command (${_variant}). Output:\n${_output}")
    endif()
  endif()
endforeach()
