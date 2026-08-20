# Requires:
#  -DBUILD_ROOT=<path>
#  -DGENTEST_SOURCE_DIR=<path to gentest source tree>
#  -DPROG=<path to gentest_codegen>
#
# Regression test for the module-mapping flags gentest_codegen preserves when it
# builds a clang-scan-deps command.
#
# CMake's Ninja dyndep pipeline spells the module mapping with the value as a
# separate argument ("-fmodule-file <name>=<path>", "-fprebuilt-module-path
# <dir>"). Clang's option table does not know that spelling, so handing it to
# clang-scan-deps verbatim fails the whole scan before any dependency is
# discovered:
#
#   error: unknown argument: '-fmodule-file'
#   error: no such file or directory: 'gentest=<path>'
#
# The joined spelling is accepted, so the scan command must carry that one. This
# is not the orphaned-flag defect covered by CheckCodegenSeparateModuleFlagStrip:
# it fires on the source's own compile command, with the value fully intact,
# before LLVM borrows anything.
#
# The external module is still kept out of the compilation database, because the
# borrowed command exercises the other half of the same code path: interpolation
# classifies the value as an input and drops it, and the orphaned flag that
# remains must be dropped rather than forwarded as a bare unknown argument.

if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenScanDepsJoinedModuleFile.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenScanDepsJoinedModuleFile.cmake: GENTEST_SOURCE_DIR not set")
endif()
if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenScanDepsJoinedModuleFile.cmake: PROG not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckFixtureWriteHelpers.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

gentest_resolve_clang_fixture_compilers(_clang _clangxx)
if(NOT _clang OR NOT _clangxx)
  gentest_skip_test("scan-deps joined module file: clang/clang++ not found")
  return()
endif()

gentest_find_clang_scan_deps(_clang_scan_deps "${_clangxx}")
if("${_clang_scan_deps}" STREQUAL "")
  gentest_skip_test("scan-deps joined module file: clang-scan-deps not found")
  return()
endif()

set(_work_dir "${BUILD_ROOT}/codegen_scan_deps_joined_module_file")
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

# module_file    -- "-fmodule-file <name>=<path>", the spelling CMake's Ninja
#                   dyndep pipeline emits.
# prebuilt_path  -- "-fprebuilt-module-path <dir>", the same hazard on the other
#                   preserved flag.
#
# In both variants the flag is followed by a further flag, so that once
# interpolation drops the value for the external module's borrowed command the
# orphan sits directly in front of it.
set(_variants "module_file" "prebuilt_path")
foreach(_variant IN LISTS _variants)
  set(_case_dir "${_work_dir}/case_${_variant}")
  set(_generated_dir "${_case_dir}/generated")
  set(_consumer "${_case_dir}/consumer.cppm")
  set(_bmi_dir "${_case_dir}/bmi")
  set(_compdb "${_case_dir}/compile_commands.json")

  file(MAKE_DIRECTORY "${_generated_dir}" "${_bmi_dir}")

  gentest_fixture_write_file("${_consumer}" [=[
module;

export module gentest.consumer_cases;

import gentest;

export namespace gentest::consumer_cases {

[[using gentest: test("scan/joined_module_file")]]
void scans_with_preserved_module_mapping() {
  (void)gentest::gentest_version();
}

} // namespace gentest::consumer_cases
]=])

  if(_variant STREQUAL "module_file")
    set(_flag "-fmodule-file")
    set(_value "gentest=${_bmi_dir}/gentest.pcm")
  else()
    set(_flag "-fprebuilt-module-path")
    set(_value "${_bmi_dir}")
  endif()

  gentest_fixture_make_compdb_entry(
    _entry
    DIRECTORY "${_case_dir}"
    FILE "${_consumer}"
    ARGUMENTS
      "clang++"
      "-std=c++20"
      "${_flag}"
      "${_value}"
      "-I${_external_dir}"
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
    "-Wno-unknown-warning-option")
  message(STATUS "Run gentest_codegen for scan-deps joined module file regression (${_variant})...")
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
  string(FIND "${_header_text}" "scans_with_preserved_module_mapping" _header_pos)
  if(_header_pos EQUAL -1)
    message(FATAL_ERROR "Expected generated header to contain the imported-module test registration (${_variant})")
  endif()

  if(NOT EXISTS "${_generated_dir}/tu_0000_consumer.registration.gentest.cpp")
    message(FATAL_ERROR "Expected module registration translation unit to be generated (${_variant})")
  endif()

  if(NOT EXISTS "${_generated_dir}/artifact_manifest.json")
    message(FATAL_ERROR "Expected artifact manifest to be generated (${_variant})")
  endif()

  string(FIND "${_output}" "unknown argument" _unknown_pos)
  if(NOT _unknown_pos EQUAL -1)
    message(FATAL_ERROR
      "Expected no 'unknown argument' symptom in codegen output (${_variant}). Output:\n${_output}")
  endif()

  string(FIND "${_output}" "no such file or directory" _symptom_pos)
  if(NOT _symptom_pos EQUAL -1)
    message(FATAL_ERROR
      "Expected no 'no such file or directory' symptom in codegen output (${_variant}). Output:\n${_output}")
  endif()

  string(FIND "${_output}" "failed to resolve named-module dependencies" _scan_fail_pos)
  if(NOT _scan_fail_pos EQUAL -1)
    message(FATAL_ERROR
      "Expected the named-module dependency scan to succeed (${_variant}). Output:\n${_output}")
  endif()
endforeach()
