# Requires:
#  -DBUILD_ROOT=<path>
#  -DGENTEST_SOURCE_DIR=<path to gentest source tree>
#  -DPROG=<path to gentest_codegen>

if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenCompileCommandMacroScan.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenCompileCommandMacroScan.cmake: GENTEST_SOURCE_DIR not set")
endif()
if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenCompileCommandMacroScan.cmake: PROG not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckFixtureWriteHelpers.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

set(_work_dir "${BUILD_ROOT}/codegen_compile_command_macro_scan")
set(_generated_missing_dir "${_work_dir}/generated_missing")
set(_generated_real_dir "${_work_dir}/generated_real")
set(_generated_bad_dir "${_work_dir}/generated_bad")
file(TO_CMAKE_PATH "${_generated_missing_dir}/tu_0000_consumer.module.gentest.cppm" _missing_wrapper_abs)
file(TO_CMAKE_PATH "${_generated_real_dir}/tu_0000_consumer.module.gentest.cppm" _real_wrapper_abs)
file(TO_CMAKE_PATH "${_generated_bad_dir}/tu_0000_consumer.module.gentest.cppm" _bad_wrapper_abs)
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}" "${_generated_missing_dir}" "${_generated_real_dir}" "${_generated_bad_dir}")

gentest_resolve_clang_fixture_compilers(_clang _clangxx)
if(NOT _clang OR NOT _clangxx)
  gentest_skip_test("compile-command macro scan regression: clang/clang++ not found")
  return()
endif()
gentest_clang_major_version(_clang_major "${_clangxx}")
if(NOT "${_clang_major}" STREQUAL "" AND NOT _clang_major VERSION_LESS 23)
  gentest_skip_test(
    "compile-command macro scan regression: Clang ${_clang_major} rejects module declarations controlled by preprocessor conditionals")
  return()
endif()
gentest_find_clang_scan_deps(_scan_deps "${_clangxx}")

set(_provider "${_work_dir}/provider.cpp")
set(_consumer "${_work_dir}/consumer.cpp")
set(_provider_obj "${_work_dir}/provider.o")
set(_consumer_obj "${_work_dir}/consumer.o")
set(_compdb "${_work_dir}/compile_commands.json")

gentest_fixture_write_file("${_provider}" [=[
module;

#if defined(GENTEST_SCAN_ENABLE_PROVIDER)
export module gentest.scan.provider;
export int provider_value() { return 7; }
#endif
]=])

gentest_fixture_write_file("${_consumer}" [=[
#include <gentest/runner.h>

#if defined(GENTEST_SCAN_ENABLE_CONSUMER) && !defined(GENTEST_SCAN_ENABLE_PROVIDER)
import gentest.scan.provider;
#endif

[[using gentest: test("scan/macro_module_import")]]
void macro_module_import() {
    gentest::expect(provider_value() == 7);
}
]=])

gentest_make_public_api_compile_args(
  _common_args
  COMPILER "${_clangxx}"
  STD "-std=c++20"
  SOURCE_ROOT "${GENTEST_SOURCE_DIR}"
  INCLUDE_TESTS
  APPLE_SYSROOT)
get_filename_component(_clangxx_name "${_clangxx}" NAME_WE)
string(TOLOWER "${_clangxx_name}" _clangxx_name_lower)
if(_clangxx_name_lower STREQUAL "cl" OR _clangxx_name_lower STREQUAL "clang-cl")
  gentest_skip_test("compile-command macro scan regression: requires GNU-style clang driver, got '${_clangxx}'")
  return()
endif()

set(_provider_define "-DGENTEST_SCAN_ENABLE_PROVIDER=1")
set(_consumer_define_flag "-D")
set(_consumer_define_value "GENTEST_SCAN_ENABLE_CONSUMER=1")
set(_consumer_undef_flag "-U")
set(_consumer_undef_value "GENTEST_SCAN_ENABLE_PROVIDER")

gentest_fixture_make_compdb_entry(
  _provider_entry
  DIRECTORY "${_work_dir}"
  FILE "${_provider}"
  ARGUMENTS ${_common_args} "${_provider_define}" "-c" "${_provider}" "-o" "${_provider_obj}")
gentest_fixture_make_compdb_entry(
  _consumer_entry
  DIRECTORY "${_work_dir}"
  FILE "${_consumer}"
  ARGUMENTS ${_common_args}
    "${_consumer_define_flag}" "${_consumer_define_value}"
    "-DGENTEST_SCAN_ENABLE_PROVIDER=1"
    "${_consumer_undef_flag}" "${_consumer_undef_value}"
    "-c" "${_consumer}" "-o" "${_consumer_obj}")
gentest_fixture_write_compdb("${_compdb}" "${_provider_entry}" "${_consumer_entry}")

message(STATUS "Require clang-scan-deps for compile-command-defined module imports...")
execute_process(
  COMMAND
    "${PROG}"
    --compdb "${_work_dir}"
    --tu-out-dir "${_generated_missing_dir}"
    --module-wrapper-output "${_missing_wrapper_abs}"
    "${_consumer}"
  WORKING_DIRECTORY "${_work_dir}"
  RESULT_VARIABLE _missing_rc
  OUTPUT_VARIABLE _missing_out
  ERROR_VARIABLE _missing_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
if(_missing_rc EQUAL 0)
  message(FATAL_ERROR "Expected named-module codegen without clang-scan-deps to fail")
endif()
set(_missing_all "${_missing_out}\n${_missing_err}")
string(FIND "${_missing_all}" "named-module codegen requires clang-scan-deps" _missing_message_pos)
if(_missing_message_pos EQUAL -1)
  message(FATAL_ERROR "Expected missing clang-scan-deps guidance. Output:\n${_missing_all}")
endif()

if(_scan_deps)
  message(STATUS "Run gentest_codegen with compile-command-defined module/import guards and required clang-scan-deps...")
  gentest_check_run_or_fail(
    COMMAND
      "${CMAKE_COMMAND}" -E env
      "GENTEST_CODEGEN_LOG_SCAN_DEPS=1"
      "${PROG}"
      --compdb "${_work_dir}"
      --clang-scan-deps "${_scan_deps}"
      --tu-out-dir "${_generated_real_dir}"
      --module-wrapper-output "${_real_wrapper_abs}"
      "${_consumer}"
    WORKING_DIRECTORY "${_work_dir}"
    OUTPUT_VARIABLE _real_output
    STRIP_TRAILING_WHITESPACE)

  string(FIND "${_real_output}" "gentest_codegen: info: using clang-scan-deps for named-module dependency discovery" _real_scan_deps_pos)
  if(_real_scan_deps_pos EQUAL -1)
    message(FATAL_ERROR
      "Expected gentest_codegen to report actual clang-scan-deps usage. Output:\n${_real_output}")
  endif()

  file(GLOB _provider_pcm_candidates "${_generated_real_dir}/.gentest_codegen_modules_*/ext_*.pcm")
  if(NOT _provider_pcm_candidates)
    message(FATAL_ERROR
      "Expected gentest_codegen to precompile the externally discovered macro-guarded provider module")
  endif()
  if(NOT EXISTS "${_generated_real_dir}/consumer.h")
    message(FATAL_ERROR "Expected registration header for the macro-guarded consumer")
  endif()
endif()

set(_bad_scan_deps "${_work_dir}/missing-clang-scan-deps")
message(STATUS "Reject an unusable required clang-scan-deps executable...")
execute_process(
  COMMAND
    "${CMAKE_COMMAND}" -E env
    "CMAKE_BUILD_PARALLEL_LEVEL=2"
    "CTEST_PARALLEL_LEVEL=2"
    "${PROG}"
    --compdb "${_work_dir}"
    --clang-scan-deps "${_bad_scan_deps}"
    --tu-out-dir "${_generated_bad_dir}"
    --module-wrapper-output "${_bad_wrapper_abs}"
    "${_consumer}"
  WORKING_DIRECTORY "${_work_dir}"
  RESULT_VARIABLE _scan_deps_on_rc
  OUTPUT_VARIABLE _scan_deps_on_out
  ERROR_VARIABLE _scan_deps_on_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

if(_scan_deps_on_rc EQUAL 0)
  message(FATAL_ERROR "Expected gentest_codegen to fail when required clang-scan-deps is unavailable")
endif()

set(_scan_deps_on_all "${_scan_deps_on_out}\n${_scan_deps_on_err}")
string(FIND "${_scan_deps_on_all}" "failed to resolve named-module dependencies via clang-scan-deps" _scan_deps_on_pos)
if(_scan_deps_on_pos EQUAL -1)
  message(FATAL_ERROR "Expected a required clang-scan-deps failure. Output:\n${_scan_deps_on_all}")
endif()
