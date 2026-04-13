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
set(_generated_off_dir "${_work_dir}/generated_off")
set(_generated_auto_dir "${_work_dir}/generated_auto")
set(_generated_on_real_dir "${_work_dir}/generated_on_real")
set(_generated_on_bad_dir "${_work_dir}/generated_on_bad")
file(TO_CMAKE_PATH "${_generated_off_dir}/tu_0000_consumer.module.gentest.cppm" _off_wrapper_abs)
file(TO_CMAKE_PATH "${_generated_auto_dir}/tu_0000_consumer.module.gentest.cppm" _auto_wrapper_abs)
file(TO_CMAKE_PATH "${_generated_on_real_dir}/tu_0000_consumer.module.gentest.cppm" _on_real_wrapper_abs)
file(TO_CMAKE_PATH "${_generated_on_bad_dir}/tu_0000_consumer.module.gentest.cppm" _on_bad_wrapper_abs)
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}" "${_generated_off_dir}" "${_generated_auto_dir}" "${_generated_on_real_dir}" "${_generated_on_bad_dir}")

gentest_resolve_clang_fixture_compilers(_clang _clangxx)
if(NOT _clang OR NOT _clangxx)
  gentest_skip_test("compile-command macro scan regression: clang/clang++ not found")
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

message(STATUS "Run gentest_codegen with compile-command-defined module/import guards and scan-deps OFF...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}" -E env
    "GENTEST_CODEGEN_LOG_MODULE_IMPORTS=1"
    "${PROG}"
    --compdb "${_work_dir}"
    --scan-deps-mode=OFF
    --tu-out-dir "${_generated_off_dir}"
    --module-wrapper-output "${_off_wrapper_abs}"
    "${_consumer}"
  WORKING_DIRECTORY "${_work_dir}"
  OUTPUT_VARIABLE _off_output
  STRIP_TRAILING_WHITESPACE)

string(FIND "${_off_output}" "gentest_codegen: source-scanned module imports for '${_consumer}':" _off_log_pos)
if(_off_log_pos EQUAL -1)
  message(FATAL_ERROR
    "Expected gentest_codegen scan-deps OFF leg to log the source-scanned module imports for the consumer source. Output:\n${_off_output}")
endif()

string(FIND "${_off_output}" "  gentest.scan.provider" _off_import_pos)
if(_off_import_pos EQUAL -1)
  message(FATAL_ERROR
    "Expected gentest_codegen scan-deps OFF leg to log the discovered gentest.scan.provider import. Output:\n${_off_output}")
endif()

file(GLOB _provider_pcm_candidates "${_generated_off_dir}/.gentest_codegen_modules_*/ext_*.pcm")
if(NOT _provider_pcm_candidates)
  message(FATAL_ERROR
    "Expected gentest_codegen to precompile the externally discovered macro-guarded provider module, which proves the consumer-side guarded import was discovered")
endif()
if(NOT EXISTS "${_generated_off_dir}/consumer.h")
  message(FATAL_ERROR "Expected registration header for macro-guarded consumer to be generated")
endif()

# Windows-specific split /D and /U handling is covered by
# CheckCodegenCompileCommandConditionWarnings.cmake. This helper keeps the
# end-to-end macro-guarded import path on the stable GNU-style Clang flow so it
# can assert successful external-module precompile plus header generation.

set(_bad_scan_deps "${_work_dir}/missing-clang-scan-deps")

message(STATUS "Run gentest_codegen with compile-command-defined module/import guards and scan-deps AUTO fallback...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}" -E env
    "GENTEST_CODEGEN_LOG_SCAN_DEPS=1"
    "${PROG}"
    --compdb "${_work_dir}"
    --scan-deps-mode=AUTO
    --clang-scan-deps "${_bad_scan_deps}"
    --tu-out-dir "${_generated_auto_dir}"
    --module-wrapper-output "${_auto_wrapper_abs}"
    "${_consumer}"
  WORKING_DIRECTORY "${_work_dir}"
  OUTPUT_VARIABLE _auto_output
  STRIP_TRAILING_WHITESPACE)

string(FIND "${_auto_output}" "gentest_codegen: info: falling back to source-scan named-module discovery" _auto_fallback_pos)
if(_auto_fallback_pos EQUAL -1)
  message(FATAL_ERROR
    "Expected gentest_codegen AUTO fallback leg to report source-scan fallback. Output:\n${_auto_output}")
endif()

file(GLOB _provider_pcm_auto_candidates "${_generated_auto_dir}/.gentest_codegen_modules_*/ext_*.pcm")
if(NOT _provider_pcm_auto_candidates)
  message(FATAL_ERROR
    "Expected gentest_codegen AUTO fallback to precompile the externally discovered macro-guarded provider module")
endif()
if(NOT EXISTS "${_generated_auto_dir}/consumer.h")
  message(FATAL_ERROR "Expected AUTO fallback to generate the registration header for the macro-guarded consumer")
endif()

if(_scan_deps)
  message(STATUS "Run gentest_codegen with compile-command-defined module/import guards and scan-deps ON...")
  gentest_check_run_or_fail(
    COMMAND
      "${CMAKE_COMMAND}" -E env
      "GENTEST_CODEGEN_LOG_SCAN_DEPS=1"
      "${PROG}"
      --compdb "${_work_dir}"
      --scan-deps-mode=ON
      --clang-scan-deps "${_scan_deps}"
      --tu-out-dir "${_generated_on_real_dir}"
      --module-wrapper-output "${_on_real_wrapper_abs}"
      "${_consumer}"
    WORKING_DIRECTORY "${_work_dir}"
    OUTPUT_VARIABLE _on_real_output
    STRIP_TRAILING_WHITESPACE)

  string(FIND "${_on_real_output}" "gentest_codegen: info: using clang-scan-deps for named-module dependency discovery" _on_real_scan_deps_pos)
  if(_on_real_scan_deps_pos EQUAL -1)
    message(FATAL_ERROR
      "Expected gentest_codegen scan-deps ON leg to report actual clang-scan-deps usage. Output:\n${_on_real_output}")
  endif()

  file(GLOB _provider_pcm_on_real_candidates "${_generated_on_real_dir}/.gentest_codegen_modules_*/ext_*.pcm")
  if(NOT _provider_pcm_on_real_candidates)
    message(FATAL_ERROR
      "Expected gentest_codegen scan-deps ON mode to precompile the externally discovered macro-guarded provider module")
  endif()
  if(NOT EXISTS "${_generated_on_real_dir}/consumer.h")
    message(FATAL_ERROR "Expected scan-deps ON mode to generate the registration header for the macro-guarded consumer")
  endif()
endif()

message(STATUS "Run gentest_codegen with compile-command-defined module/import guards and scan-deps ON failure...")
execute_process(
  COMMAND
    "${CMAKE_COMMAND}" -E env
    "CMAKE_BUILD_PARALLEL_LEVEL=2"
    "CTEST_PARALLEL_LEVEL=2"
    "${PROG}"
    --compdb "${_work_dir}"
    --scan-deps-mode=ON
    --clang-scan-deps "${_bad_scan_deps}"
    --tu-out-dir "${_generated_on_bad_dir}"
    --module-wrapper-output "${_on_bad_wrapper_abs}"
    "${_consumer}"
  WORKING_DIRECTORY "${_work_dir}"
  RESULT_VARIABLE _scan_deps_on_rc
  OUTPUT_VARIABLE _scan_deps_on_out
  ERROR_VARIABLE _scan_deps_on_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

if(_scan_deps_on_rc EQUAL 0)
  message(FATAL_ERROR
    "Expected gentest_codegen scan-deps ON mode to fail when clang-scan-deps is unavailable")
endif()

set(_scan_deps_on_all "${_scan_deps_on_out}\n${_scan_deps_on_err}")
string(FIND "${_scan_deps_on_all}" "failed to resolve named-module dependencies via clang-scan-deps (mode=ON)" _scan_deps_on_pos)
if(_scan_deps_on_pos EQUAL -1)
  message(FATAL_ERROR
    "Expected gentest_codegen scan-deps ON mode to report a clang-scan-deps failure. Output:\n${_scan_deps_on_all}")
endif()
