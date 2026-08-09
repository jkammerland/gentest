# Requires:
#  -DSOURCE_DIR=<fixture source dir>
#  -DBUILD_ROOT=<path to parent build dir>
#  -DGENTEST_SOURCE_DIR=<path to gentest source tree>
# Optional:
#  -DGENERATOR=<cmake generator name>
#  -DGENERATOR_PLATFORM=<platform>
#  -DGENERATOR_TOOLSET=<toolset>
#  -DTOOLCHAIN_FILE=<toolchain.cmake>
#  -DMAKE_PROGRAM=<path>
#  -DC_COMPILER=<path>
#  -DCXX_COMPILER=<path>
#  -DBUILD_TYPE=<Debug|Release|...>

if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckModulePcmCacheIsolation.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckModulePcmCacheIsolation.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckModulePcmCacheIsolation.cmake: GENTEST_SOURCE_DIR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

if(CMAKE_HOST_WIN32)
  set(_work_dir "${BUILD_ROOT}/mpci")
else()
  set(_work_dir "${BUILD_ROOT}/module_pcm_cache_isolation")
endif()
set(_src_dir "${_work_dir}/src")
set(_build_dir "${_work_dir}/build")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(COPY "${SOURCE_DIR}/" DESTINATION "${_src_dir}")

function(_gentest_expect_equal actual expected label)
  if(NOT "${actual}" STREQUAL "${expected}")
    message(FATAL_ERROR "${label}: expected '${expected}', got '${actual}'")
  endif()
endfunction()

function(_gentest_expect_dot_module_cache_state timing_path expected_state)
  _gentest_expect_pcm_cache_state("${timing_path}" "${expected_state}" "gentest.pcm_cache.alpha.beta.provider")
  _gentest_expect_pcm_cache_state("${timing_path}" "${expected_state}" "gentest.pcm_cache.alpha.beta")
endfunction()

function(_gentest_get_pcm_cache_state timing_path module_name out_var)
  if(NOT EXISTS "${timing_path}")
    message(FATAL_ERROR "Expected PCM timing JSON '${timing_path}'")
  endif()
  file(READ "${timing_path}" _timing_json)
  string(JSON _phase_count LENGTH "${_timing_json}" phases)
  foreach(_phase_index RANGE 0 ${_phase_count})
    if(_phase_index EQUAL _phase_count)
      break()
    endif()
    string(JSON _phase_name GET "${_timing_json}" phases ${_phase_index} name)
    if(NOT _phase_name STREQUAL "pcm")
      continue()
    endif()
    string(JSON _module ERROR_VARIABLE _module_error GET "${_timing_json}" phases ${_phase_index} module)
    if(NOT _module_error STREQUAL "NOTFOUND" OR NOT _module STREQUAL "${module_name}")
      continue()
    endif()
    string(JSON _cache ERROR_VARIABLE _cache_error GET "${_timing_json}" phases ${_phase_index} cache)
    if(NOT _cache_error STREQUAL "NOTFOUND")
      message(FATAL_ERROR "PCM timing record for '${module_name}' has no cache state. Timing JSON:\n${_timing_json}")
    endif()
    set(${out_var} "${_cache}" PARENT_SCOPE)
    return()
  endforeach()
  message(FATAL_ERROR "Expected PCM timing record for module '${module_name}'. Timing JSON:\n${_timing_json}")
endfunction()

function(_gentest_expect_provider_pcm_build_root timing_path expected_value label)
  file(READ "${timing_path}" _timing_json)
  string(JSON _phase_count LENGTH "${_timing_json}" phases)
  set(_provider_pcm "")
  foreach(_phase_index RANGE 0 ${_phase_count})
    if(_phase_index EQUAL _phase_count)
      break()
    endif()
    string(JSON _phase_name GET "${_timing_json}" phases ${_phase_index} name)
    if(NOT _phase_name STREQUAL "pcm")
      continue()
    endif()
    string(JSON _module ERROR_VARIABLE _module_error GET "${_timing_json}" phases ${_phase_index} module)
    if(NOT _module_error STREQUAL "NOTFOUND" OR NOT _module STREQUAL "gentest.pcm_cache.alpha.beta.provider")
      continue()
    endif()
    string(JSON _provider_pcm GET "${_timing_json}" phases ${_phase_index} path)
    break()
  endforeach()
  if("${_provider_pcm}" STREQUAL "" OR NOT EXISTS "${_provider_pcm}")
    message(FATAL_ERROR "${label}: provider PCM was not recorded in '${timing_path}'")
  endif()

  string(MD5 _verify_id "${timing_path};${expected_value}")
  set(_verify_source "${_work_dir}/pcm_build_root_${_verify_id}.cpp")
  file(WRITE "${_verify_source}" [=[
import gentest.pcm_cache.alpha.beta.provider;

constexpr bool gentest_pcm_strings_match(const char* lhs, const char* rhs) {
  while(*lhs != '\0' && *rhs != '\0') {
    if(*lhs != *rhs) {
      return false;
    }
    ++lhs;
    ++rhs;
  }
  return *lhs == *rhs;
}

static_assert(gentest_pcm_strings_match(dot_provider::kBuildRoot, PCM_EXPECTED_BUILD_ROOT));
]=])
  execute_process(
    COMMAND
      "${_clangxx}"
      -std=c++20
      "-fmodule-file=gentest.pcm_cache.alpha.beta.provider=${_provider_pcm}"
      "-DPCM_EXPECTED_BUILD_ROOT=\"${expected_value}\""
      -fsyntax-only
      "${_verify_source}"
    WORKING_DIRECTORY "${_work_dir}"
    RESULT_VARIABLE _verify_rc
    OUTPUT_VARIABLE _verify_out
    ERROR_VARIABLE _verify_err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)
  if(NOT _verify_rc EQUAL 0)
    message(FATAL_ERROR "${label}: provider PCM did not export the expected build-root value.\n--- stdout ---\n${_verify_out}\n--- stderr ---\n${_verify_err}")
  endif()
endfunction()

function(_gentest_shell_quote out_var value)
  string(REPLACE "'" "'\"'\"'" _escaped "${value}")
  set(${out_var} "'${_escaped}'" PARENT_SCOPE)
endfunction()

function(_gentest_concurrent_codegen_command out_var output_stem output_root cache_dir)
  set(_tu_output_dir "${output_root}/${output_stem}")
  set(_args
    "${_codegen_exe}"
    --pcm-cache-dir "${cache_dir}"
    --tu-out-dir "${_tu_output_dir}"
    --tu-header-output "${_tu_output_dir}/tu_0.gentest.h"
    --module-wrapper-output "${_tu_output_dir}/tu_0.module.gentest.cppm"
    --tu-header-output "${_tu_output_dir}/tu_1.gentest.h"
    --module-wrapper-output "${_tu_output_dir}/tu_1.module.gentest.cppm"
    --tu-header-output "${_tu_output_dir}/tu_2.gentest.h"
    --module-wrapper-output "${_tu_output_dir}/tu_2.module.gentest.cppm"
    --mock-registry "${output_root}/${output_stem}_mock_registry.hpp"
    --mock-impl "${output_root}/${output_stem}_mock_impl.hpp"
    --mock-domain-registry-output "${output_root}/${output_stem}_mock_registry_domain_header.hpp"
    --mock-domain-registry-output "${output_root}/${output_stem}_mock_registry_domain_a.hpp"
    --mock-domain-registry-output "${output_root}/${output_stem}_mock_registry_domain_b.hpp"
    --mock-domain-registry-output "${output_root}/${output_stem}_mock_registry_domain_root.hpp"
    --mock-domain-impl-output "${output_root}/${output_stem}_mock_impl_domain_header.hpp"
    --mock-domain-impl-output "${output_root}/${output_stem}_mock_impl_domain_a.hpp"
    --mock-domain-impl-output "${output_root}/${output_stem}_mock_impl_domain_b.hpp"
    --mock-domain-impl-output "${output_root}/${output_stem}_mock_impl_domain_root.hpp"
    --depfile "${output_root}/${output_stem}.gentest.d"
    --compdb "${_build_dir}"
    --source-root "${_src_dir}"
    ${_dot_sources}
    --
    -std=c++20
    -x c++-module
    -DGENTEST_CODEGEN=1)
  set(_quoted_args)
  foreach(_arg IN LISTS _args)
    _gentest_shell_quote(_quoted_arg "${_arg}")
    list(APPEND _quoted_args "${_quoted_arg}")
  endforeach()
  string(JOIN " " _command ${_quoted_args})
  set(${out_var} "${_command}" PARENT_SCOPE)
endfunction()

gentest_resolve_clang_fixture_compilers(_clang _clangxx)

if(NOT _clang OR NOT _clangxx)
  gentest_skip_test("module PCM cache isolation regression: no usable clang/clang++ pair was provided")
  return()
endif()

set(_cmake_gen_args -G "${GENERATOR}")
if(DEFINED GENERATOR_PLATFORM AND NOT "${GENERATOR_PLATFORM}" STREQUAL "")
  list(APPEND _cmake_gen_args -A "${GENERATOR_PLATFORM}")
endif()
if(DEFINED GENERATOR_TOOLSET AND NOT "${GENERATOR_TOOLSET}" STREQUAL "")
  list(APPEND _cmake_gen_args -T "${GENERATOR_TOOLSET}")
endif()

set(_cmake_cache_args
  "-DGENTEST_SOURCE_DIR=${GENTEST_SOURCE_DIR}"
  "-DCMAKE_C_COMPILER=${_clang}"
  "-DCMAKE_CXX_COMPILER=${_clangxx}")
if(GENERATOR STREQUAL "Ninja" OR GENERATOR STREQUAL "Ninja Multi-Config")
  gentest_find_supported_ninja(_supported_ninja _supported_ninja_reason)
  if(NOT _supported_ninja)
    gentest_skip_test("module PCM cache isolation regression: ${_supported_ninja_reason}")
    return()
  endif()
  list(APPEND _cmake_cache_args "-DCMAKE_MAKE_PROGRAM=${_supported_ninja}")
elseif(DEFINED MAKE_PROGRAM AND NOT "${MAKE_PROGRAM}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()
if(DEFINED TOOLCHAIN_FILE AND NOT "${TOOLCHAIN_FILE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}")
endif()
if(DEFINED LLVM_DIR AND NOT "${LLVM_DIR}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DLLVM_DIR=${LLVM_DIR}")
endif()
if(DEFINED Clang_DIR AND NOT "${Clang_DIR}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DClang_DIR=${Clang_DIR}")
endif()
if(DEFINED PROG AND NOT "${PROG}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DGENTEST_CODEGEN_EXECUTABLE=${PROG}")
endif()
gentest_find_clang_scan_deps(_clang_scan_deps "${_clangxx}")
if(NOT "${_clang_scan_deps}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=${_clang_scan_deps}")
endif()
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()
gentest_append_public_modules_cache_arg(_cmake_cache_args)
gentest_append_host_apple_sysroot(_cmake_cache_args)

message(STATUS "Configure shared-build-tree module PCM cache fixture...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${_src_dir}"
    -B "${_build_dir}"
    ${_cmake_cache_args}
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

set(_generated_dir "${_build_dir}/generated")
file(MAKE_DIRECTORY "${_generated_dir}")
set(_validated_pcm_cache "${_generated_dir}/validated_pcm_cache")
set(_codegen_exe_ext "")
if(CMAKE_HOST_WIN32)
  set(_codegen_exe_ext ".exe")
endif()
set(_codegen_exe "")
if(DEFINED PROG AND NOT "${PROG}" STREQUAL "")
  set(_codegen_exe "${PROG}")
else()
  set(_codegen_exe "${_build_dir}/gentest/tools/gentest_codegen${_codegen_exe_ext}")
  if(NOT EXISTS "${_codegen_exe}")
    message(FATAL_ERROR "gentest_codegen executable not found: '${_codegen_exe}'")
  endif()
endif()
set(_dot_sources
  "${_src_dir}/alpha_dot_provider.cppm"
  "${_src_dir}/alpha_dot_consumer.cppm"
  "${_src_dir}/alpha_dot_root.cppm")

function(_gentest_run_codegen_fixture output_stem)
  set(options LOG_SCAN_DEPS)
  set(one_value_args PCM_CACHE PCM_CACHE_DIR TIMING_JSON COMPDB_DIR OUTPUT_ROOT CACHE_SALT OUTPUT_VARIABLE)
  set(multi_value_args SOURCES EXTRA_ARGS)
  cmake_parse_arguments(GENTEST "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

  if(NOT GENTEST_SOURCES)
    message(FATAL_ERROR "_gentest_run_codegen_fixture requires at least one source")
  endif()
  if(output_stem STREQUAL "pcm_cache_dot_generated")
    list(FIND GENTEST_SOURCES "${_src_dir}/alpha_dot_root.cppm" _dot_root_index)
    if(_dot_root_index EQUAL -1)
      list(APPEND GENTEST_SOURCES "${_src_dir}/alpha_dot_root.cppm")
    endif()
  endif()

  set(_output_root "${_generated_dir}")
  if(NOT "${GENTEST_OUTPUT_ROOT}" STREQUAL "")
    set(_output_root "${GENTEST_OUTPUT_ROOT}")
  endif()
  set(_compdb_dir "${_build_dir}")
  if(NOT "${GENTEST_COMPDB_DIR}" STREQUAL "")
    set(_compdb_dir "${GENTEST_COMPDB_DIR}")
  endif()
  set(_tu_output_dir "${_output_root}/${output_stem}")
  set(_mock_registry "${_output_root}/${output_stem}_mock_registry.hpp")
  set(_mock_impl "${_output_root}/${output_stem}_mock_impl.hpp")
  set(_mock_registry_header_domain "${_output_root}/${output_stem}_mock_registry_domain_header.hpp")
  set(_mock_impl_header_domain "${_output_root}/${output_stem}_mock_impl_domain_header.hpp")
  set(_mock_registry_module_domain_a "${_output_root}/${output_stem}_mock_registry_domain_a.hpp")
  set(_mock_impl_module_domain_a "${_output_root}/${output_stem}_mock_impl_domain_a.hpp")
  set(_mock_registry_module_domain_b "${_output_root}/${output_stem}_mock_registry_domain_b.hpp")
  set(_mock_impl_module_domain_b "${_output_root}/${output_stem}_mock_impl_domain_b.hpp")
  set(_mock_registry_module_domain_root "${_output_root}/${output_stem}_mock_registry_domain_root.hpp")
  set(_mock_impl_module_domain_root "${_output_root}/${output_stem}_mock_impl_domain_root.hpp")
  set(_depfile "${_output_root}/${output_stem}.gentest.d")
  set(_root_domain_args)
  if(output_stem STREQUAL "pcm_cache_dot_generated")
    list(APPEND
      _root_domain_args
      --mock-domain-registry-output "${_mock_registry_module_domain_root}"
      --mock-domain-impl-output "${_mock_impl_module_domain_root}")
  endif()
  file(MAKE_DIRECTORY "${_tu_output_dir}")

  set(_tu_output_args)
  set(_source_index 0)
  foreach(_source IN LISTS GENTEST_SOURCES)
    list(APPEND
      _tu_output_args
      --tu-header-output
      "${_tu_output_dir}/tu_${_source_index}.gentest.h"
      --module-wrapper-output
      "${_tu_output_dir}/tu_${_source_index}.module.gentest.cppm")
    math(EXPR _source_index "${_source_index} + 1")
  endforeach()
  unset(_source)
  unset(_source_index)

  set(_pcm_cache_args)
  if(GENTEST_PCM_CACHE)
    set(_pcm_cache_dir "${_validated_pcm_cache}")
    if(NOT "${GENTEST_PCM_CACHE_DIR}" STREQUAL "")
      set(_pcm_cache_dir "${GENTEST_PCM_CACHE_DIR}")
    endif()
    list(APPEND _pcm_cache_args --pcm-cache-dir "${_pcm_cache_dir}")
  endif()
  set(_timing_args)
  if(NOT "${GENTEST_TIMING_JSON}" STREQUAL "")
    list(APPEND _timing_args --timing-json "${GENTEST_TIMING_JSON}")
  endif()
  set(_codegen_env)
  if(NOT "${GENTEST_CACHE_SALT}" STREQUAL "")
    list(APPEND _codegen_env "GENTEST_CODEGEN_PCM_CACHE_SALT=${GENTEST_CACHE_SALT}")
  endif()
  if(GENTEST_LOG_SCAN_DEPS)
    list(APPEND _codegen_env "GENTEST_CODEGEN_LOG_SCAN_DEPS=1")
  endif()
  set(_codegen_command "${_codegen_exe}")
  if(_codegen_env)
    set(_codegen_command "${CMAKE_COMMAND}" -E env ${_codegen_env} "${_codegen_exe}")
  endif()

  gentest_check_run_or_fail(
    OUTPUT_VARIABLE _codegen_output
    COMMAND
      ${_codegen_command}
      ${_pcm_cache_args}
      ${_timing_args}
      --tu-out-dir "${_tu_output_dir}"
      ${_tu_output_args}
      --mock-registry "${_mock_registry}"
      --mock-impl "${_mock_impl}"
      --mock-domain-registry-output "${_mock_registry_header_domain}"
      --mock-domain-registry-output "${_mock_registry_module_domain_a}"
      --mock-domain-registry-output "${_mock_registry_module_domain_b}"
      --mock-domain-impl-output "${_mock_impl_header_domain}"
      --mock-domain-impl-output "${_mock_impl_module_domain_a}"
      --mock-domain-impl-output "${_mock_impl_module_domain_b}"
      ${_root_domain_args}
      --depfile "${_depfile}"
      --compdb "${_compdb_dir}"
      --source-root "${_src_dir}"
      ${GENTEST_SOURCES}
      --
      -std=c++20
      -x
      c++-module
      -DGENTEST_CODEGEN=1
      ${GENTEST_EXTRA_ARGS}
    WORKING_DIRECTORY "${_work_dir}"
    STRIP_TRAILING_WHITESPACE)
  if(DEFINED ENV{GENTEST_CODEGEN_LOG_SCAN_DEPS} AND NOT "$ENV{GENTEST_CODEGEN_LOG_SCAN_DEPS}" STREQUAL "" AND
     NOT "${_codegen_output}" STREQUAL "")
    message(STATUS "${_codegen_output}")
  endif()
  if(NOT "${GENTEST_OUTPUT_VARIABLE}" STREQUAL "")
    set(${GENTEST_OUTPUT_VARIABLE} "${_codegen_output}" PARENT_SCOPE)
  endif()
endfunction()

function(_gentest_expect_pcm_cache_state timing_path expected_state)
  if(NOT EXISTS "${timing_path}")
    message(FATAL_ERROR "Expected PCM timing JSON '${timing_path}'")
  endif()
  file(READ "${timing_path}" _timing_json)
  string(JSON _phase_count LENGTH "${_timing_json}" phases)
  set(_found FALSE)
  foreach(_phase_index RANGE 0 ${_phase_count})
    if(_phase_index EQUAL _phase_count)
      break()
    endif()
    string(JSON _name GET "${_timing_json}" phases ${_phase_index} name)
    if(NOT _name STREQUAL "pcm")
      continue()
    endif()
    string(JSON _cache ERROR_VARIABLE _cache_error GET "${_timing_json}" phases ${_phase_index} cache)
    if(NOT _cache_error STREQUAL "NOTFOUND" OR NOT _cache STREQUAL "${expected_state}")
      continue()
    endif()
    if(ARGC GREATER 2)
      string(JSON _module ERROR_VARIABLE _module_error GET "${_timing_json}" phases ${_phase_index} module)
      if(NOT _module_error STREQUAL "NOTFOUND" OR NOT _module STREQUAL "${ARGV2}")
        continue()
      endif()
    endif()
    set(_found TRUE)
    break()
  endforeach()
  if(NOT _found)
    if(ARGC GREATER 2)
      message(FATAL_ERROR "Expected PCM module '${ARGV2}' with cache=${expected_state}. Timing JSON:\n${_timing_json}")
    endif()
    message(FATAL_ERROR "Expected a PCM timing record with cache=${expected_state}. Timing JSON:\n${_timing_json}")
  endif()
endfunction()

function(_gentest_expect_dot_module_timing_collision label timing_path expected_diagnostic)
  if(NOT EXISTS "${timing_path}")
    message(FATAL_ERROR "${label}: timing collision sentinel '${timing_path}' does not exist")
  endif()
  file(SHA256 "${timing_path}" _hash_before)

  set(_tu_output_dir "${_generated_dir}/pcm_cache_dot_generated")
  set(_pcm_cache_args --pcm-cache-dir "${_validated_pcm_cache}")
  if(ARGC GREATER 3 AND "${ARGV3}" STREQUAL "OFF")
    set(_pcm_cache_args)
  endif()
  execute_process(
    COMMAND
      "${_codegen_exe}"
      ${_pcm_cache_args}
      --timing-json "${timing_path}"
      --tu-out-dir "${_tu_output_dir}"
      --tu-header-output "${_tu_output_dir}/tu_0.gentest.h"
      --module-wrapper-output "${_tu_output_dir}/tu_0.module.gentest.cppm"
      --tu-header-output "${_tu_output_dir}/tu_1.gentest.h"
      --module-wrapper-output "${_tu_output_dir}/tu_1.module.gentest.cppm"
      --tu-header-output "${_tu_output_dir}/tu_2.gentest.h"
      --module-wrapper-output "${_tu_output_dir}/tu_2.module.gentest.cppm"
      --mock-registry "${_generated_dir}/pcm_cache_dot_generated_mock_registry.hpp"
      --mock-impl "${_generated_dir}/pcm_cache_dot_generated_mock_impl.hpp"
      --mock-domain-registry-output "${_generated_dir}/pcm_cache_dot_generated_mock_registry_domain_header.hpp"
      --mock-domain-registry-output "${_generated_dir}/pcm_cache_dot_generated_mock_registry_domain_a.hpp"
      --mock-domain-registry-output "${_generated_dir}/pcm_cache_dot_generated_mock_registry_domain_b.hpp"
      --mock-domain-registry-output "${_generated_dir}/pcm_cache_dot_generated_mock_registry_domain_root.hpp"
      --mock-domain-impl-output "${_generated_dir}/pcm_cache_dot_generated_mock_impl_domain_header.hpp"
      --mock-domain-impl-output "${_generated_dir}/pcm_cache_dot_generated_mock_impl_domain_a.hpp"
      --mock-domain-impl-output "${_generated_dir}/pcm_cache_dot_generated_mock_impl_domain_b.hpp"
      --mock-domain-impl-output "${_generated_dir}/pcm_cache_dot_generated_mock_impl_domain_root.hpp"
      --depfile "${_generated_dir}/pcm_cache_dot_generated.gentest.d"
      --compdb "${_build_dir}"
      --source-root "${_src_dir}"
      ${_dot_sources}
      --
      -std=c++20
      -x
      c++-module
      -DGENTEST_CODEGEN=1
    WORKING_DIRECTORY "${_work_dir}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)
  set(_all "${_out}\n${_err}")
  if(_rc EQUAL 0)
    message(FATAL_ERROR "${label}: expected failure, but it succeeded. Output:\n${_all}")
  endif()
  string(FIND "${_all}" "${expected_diagnostic}" _diagnostic_pos)
  if(_diagnostic_pos EQUAL -1)
    message(FATAL_ERROR "${label}: expected '${expected_diagnostic}'. Output:\n${_all}")
  endif()
  file(SHA256 "${timing_path}" _hash_after)
  if(NOT "${_hash_after}" STREQUAL "${_hash_before}")
    message(FATAL_ERROR "${label}: collision modified '${timing_path}' before it failed")
  endif()
endfunction()

set(_dot_disabled_timing "${_generated_dir}/dot_disabled_timing.json")
message(STATUS "Run gentest_codegen for the dot module target with PCM cache off...")
_gentest_run_codegen_fixture(
  "pcm_cache_dot_generated"
  PCM_CACHE OFF
  TIMING_JSON "${_dot_disabled_timing}"
  SOURCES
    "${_src_dir}/alpha_dot_provider.cppm"
    "${_src_dir}/alpha_dot_consumer.cppm")
_gentest_expect_dot_module_cache_state("${_dot_disabled_timing}" "disabled")

set(_dot_miss_timing "${_generated_dir}/dot_miss_timing.json")
message(STATUS "Run gentest_codegen for the dot module target with validated PCM cache (cold)...")
_gentest_run_codegen_fixture(
  "pcm_cache_dot_generated"
  PCM_CACHE ON
  LOG_SCAN_DEPS
  OUTPUT_VARIABLE _dot_miss_output
  TIMING_JSON "${_dot_miss_timing}"
  SOURCES
    "${_src_dir}/alpha_dot_provider.cppm"
    "${_src_dir}/alpha_dot_consumer.cppm")

_gentest_get_pcm_cache_state(
  "${_dot_miss_timing}"
  "gentest.pcm_cache.alpha.beta.provider"
  _dot_initial_cache_state)

# Some Windows clang-scan-deps versions produce a complete file closure but
# more than one distinct module command. Reuse is intentionally ineligible in
# that case: prove that cache-enabled generation safely stays on the local PCM
# path without publishing an entry or changing generated consumers. Supported
# scanners continue through the full miss/hit/invalidation matrix below.
if(CMAKE_HOST_WIN32 AND _dot_initial_cache_state STREQUAL "bypass")
  _gentest_expect_dot_module_cache_state("${_dot_miss_timing}" "bypass")
  foreach(_expected_log IN ITEMS
      "using clang-scan-deps for named-module dependency discovery"
      "PCM cache bypassed for 'gentest.pcm_cache.alpha.beta.provider': clang-scan-deps did not provide an unambiguous module command")
    string(FIND "${_dot_miss_output}" "${_expected_log}" _expected_log_pos)
    if(_expected_log_pos EQUAL -1)
      message(FATAL_ERROR "Windows safe-bypass lane did not report '${_expected_log}'. Output:\n${_dot_miss_output}")
    endif()
  endforeach()
  set(_dot_bypass_outputs
    "${_generated_dir}/pcm_cache_dot_generated/tu_0.gentest.h"
    "${_generated_dir}/pcm_cache_dot_generated/tu_1.gentest.h"
    "${_generated_dir}/pcm_cache_dot_generated/tu_2.gentest.h"
    "${_generated_dir}/pcm_cache_dot_generated.gentest.d")
  set(_dot_bypass_hashes "")
  foreach(_dot_output IN LISTS _dot_bypass_outputs)
    file(SHA256 "${_dot_output}" _dot_output_hash)
    list(APPEND _dot_bypass_hashes "${_dot_output_hash}")
  endforeach()

  set(_dot_bypass_repeat_timing "${_generated_dir}/dot_bypass_repeat_timing.json")
  _gentest_run_codegen_fixture(
    "pcm_cache_dot_generated"
    PCM_CACHE ON
    TIMING_JSON "${_dot_bypass_repeat_timing}"
    SOURCES
      "${_src_dir}/alpha_dot_provider.cppm"
      "${_src_dir}/alpha_dot_consumer.cppm")
  _gentest_expect_dot_module_cache_state("${_dot_bypass_repeat_timing}" "bypass")
  foreach(_dot_output _dot_expected_hash IN ZIP_LISTS _dot_bypass_outputs _dot_bypass_hashes)
    file(SHA256 "${_dot_output}" _dot_actual_hash)
    if(NOT _dot_actual_hash STREQUAL _dot_expected_hash)
      message(FATAL_ERROR "Safe PCM cache bypass changed '${_dot_output}'")
    endif()
  endforeach()

  file(GLOB_RECURSE _bypass_cache_entries LIST_DIRECTORIES FALSE "${_validated_pcm_cache}/*/module.pcm")
  list(LENGTH _bypass_cache_entries _bypass_cache_entry_count)
  _gentest_expect_equal("${_bypass_cache_entry_count}" "0" "safe-bypass validated PCM cache entry count")
  file(GLOB _bypass_local_pcm_files LIST_DIRECTORIES FALSE
    "${_generated_dir}/pcm_cache_dot_generated/.gentest_codegen_modules_*/m_*.pcm")
  list(LENGTH _bypass_local_pcm_files _bypass_local_pcm_count)
  _gentest_expect_equal("${_bypass_local_pcm_count}" "2" "safe-bypass local PCM file count")
  message(STATUS "Validated PCM cache safely bypassed an ambiguous Windows scanner command")
  return()
endif()

_gentest_expect_dot_module_cache_state("${_dot_miss_timing}" "miss")

set(_dot_stable_outputs
  "${_generated_dir}/pcm_cache_dot_generated/tu_0.gentest.h"
  "${_generated_dir}/pcm_cache_dot_generated/tu_1.gentest.h"
  "${_generated_dir}/pcm_cache_dot_generated/tu_2.gentest.h"
  "${_generated_dir}/pcm_cache_dot_generated.gentest.d")
set(_dot_stable_hashes "")
foreach(_dot_output IN LISTS _dot_stable_outputs)
  file(SHA256 "${_dot_output}" _dot_output_hash)
  list(APPEND _dot_stable_hashes "${_dot_output_hash}")
endforeach()

set(_dot_hit_timing "${_generated_dir}/dot_hit_timing.json")
message(STATUS "Run gentest_codegen for the dot module target with validated PCM cache (hit)...")
_gentest_run_codegen_fixture(
  "pcm_cache_dot_generated"
  PCM_CACHE ON
  TIMING_JSON "${_dot_hit_timing}"
  SOURCES
    "${_src_dir}/alpha_dot_provider.cppm"
    "${_src_dir}/alpha_dot_consumer.cppm")
_gentest_expect_dot_module_cache_state("${_dot_hit_timing}" "hit")
foreach(_dot_output _dot_expected_hash IN ZIP_LISTS _dot_stable_outputs _dot_stable_hashes)
  file(SHA256 "${_dot_output}" _dot_actual_hash)
  if(NOT _dot_actual_hash STREQUAL _dot_expected_hash)
    message(FATAL_ERROR "Validated PCM cache hit changed '${_dot_output}'")
  endif()
endforeach()

file(GLOB_RECURSE _validated_cache_entries LIST_DIRECTORIES FALSE "${_validated_pcm_cache}/*/module.pcm")
list(SORT _validated_cache_entries)
list(LENGTH _validated_cache_entries _validated_cache_entry_count)
if(_validated_cache_entry_count LESS 1)
  message(FATAL_ERROR "Validated PCM cache did not publish an immutable module.pcm entry")
endif()
list(GET _validated_cache_entries 0 _validated_cache_pcm)
_gentest_expect_dot_module_timing_collision(
  "timing/validated PCM cache collision"
  "${_validated_cache_pcm}"
  "validated PCM cache artifact")

# The invocation-local PCM output changes under a relocated build tree, but
# source identities/content and the normalized module command do not. A shared
# cache must still validate and reuse the provider artifact.
set(_relocated_build_dir "${_work_dir}/relocated-build")
set(_relocated_generated_dir "${_relocated_build_dir}/generated")
message(STATUS "Configure relocated module build tree for validated PCM cache reuse...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${_src_dir}"
    -B "${_relocated_build_dir}"
    ${_cmake_cache_args}
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)
set(_relocated_timing "${_relocated_generated_dir}/timing.json")
_gentest_run_codegen_fixture(
  "pcm_cache_dot_generated"
  PCM_CACHE ON
  COMPDB_DIR "${_relocated_build_dir}"
  OUTPUT_ROOT "${_relocated_generated_dir}"
  TIMING_JSON "${_relocated_timing}"
  SOURCES
    "${_src_dir}/alpha_dot_provider.cppm"
    "${_src_dir}/alpha_dot_consumer.cppm")
_gentest_expect_dot_module_cache_state("${_relocated_timing}" "hit")

# Relocation only ignores structural module-output/source spellings. A macro
# containing the build directory is semantic input and must force a miss; the
# emitted registration header makes the current macro value observable.
set(_macro_original_timing "${_generated_dir}/dot_macro_original_timing.json")
file(TO_CMAKE_PATH "${_build_dir}" _macro_original_value)
_gentest_run_codegen_fixture(
  "pcm_cache_dot_generated"
  PCM_CACHE ON
  TIMING_JSON "${_macro_original_timing}"
  EXTRA_ARGS "-DPCM_BUILD_ROOT=\"${_macro_original_value}\""
  SOURCES
    "${_src_dir}/alpha_dot_provider.cppm"
    "${_src_dir}/alpha_dot_consumer.cppm")
_gentest_expect_dot_module_cache_state("${_macro_original_timing}" "miss")
_gentest_expect_provider_pcm_build_root("${_macro_original_timing}" "${_macro_original_value}" "original macro-sensitive PCM")

set(_macro_relocated_timing "${_relocated_generated_dir}/macro_timing.json")
file(TO_CMAKE_PATH "${_relocated_build_dir}" _macro_relocated_value)
_gentest_run_codegen_fixture(
  "pcm_cache_dot_generated"
  PCM_CACHE ON
  COMPDB_DIR "${_relocated_build_dir}"
  OUTPUT_ROOT "${_relocated_generated_dir}"
  TIMING_JSON "${_macro_relocated_timing}"
  EXTRA_ARGS "-DPCM_BUILD_ROOT=\"${_macro_relocated_value}\""
  SOURCES
    "${_src_dir}/alpha_dot_provider.cppm"
    "${_src_dir}/alpha_dot_consumer.cppm")
_gentest_expect_dot_module_cache_state("${_macro_relocated_timing}" "miss")
_gentest_expect_provider_pcm_build_root("${_macro_relocated_timing}" "${_macro_relocated_value}" "relocated macro-sensitive PCM")

# Each of these changes must turn the next use into a validated miss. The
# generated test outputs themselves remain byte-stable where the source's
# observable test declaration did not change.
set(_flag_timing "${_generated_dir}/dot_flag_timing.json")
_gentest_run_codegen_fixture(
  "pcm_cache_dot_generated"
  PCM_CACHE ON
  TIMING_JSON "${_flag_timing}"
  EXTRA_ARGS -DPCM_CACHE_TEST_FLAG=1
  SOURCES
    "${_src_dir}/alpha_dot_provider.cppm"
    "${_src_dir}/alpha_dot_consumer.cppm")
_gentest_expect_dot_module_cache_state("${_flag_timing}" "miss")

set(_salt_timing "${_generated_dir}/dot_salt_timing.json")
_gentest_run_codegen_fixture(
  "pcm_cache_dot_generated"
  PCM_CACHE ON
  CACHE_SALT "fixture-salt-v1"
  TIMING_JSON "${_salt_timing}"
  SOURCES
    "${_src_dir}/alpha_dot_provider.cppm"
    "${_src_dir}/alpha_dot_consumer.cppm")
_gentest_expect_dot_module_cache_state("${_salt_timing}" "miss")

# `late` supplied the current file-deps closure during the cold lookup. Add a
# new earlier root at the same spelling, then edit that selected header. Each
# direct codegen invocation reruns scan-deps, so both are safe misses.
file(MAKE_DIRECTORY "${_src_dir}/early")
file(WRITE "${_src_dir}/early/pcm_cache_shadow.hpp" "#pragma once\ninline constexpr int gentest_pcm_cache_shadow_value = 2;\n")
set(_shadow_timing "${_generated_dir}/dot_shadow_timing.json")
_gentest_run_codegen_fixture(
  "pcm_cache_dot_generated"
  PCM_CACHE ON
  TIMING_JSON "${_shadow_timing}"
  SOURCES
    "${_src_dir}/alpha_dot_provider.cppm"
    "${_src_dir}/alpha_dot_consumer.cppm")
_gentest_expect_dot_module_cache_state("${_shadow_timing}" "miss")

file(WRITE "${_src_dir}/early/pcm_cache_shadow.hpp" "#pragma once\ninline constexpr int gentest_pcm_cache_shadow_value = 3;\n")
set(_header_timing "${_generated_dir}/dot_header_timing.json")
_gentest_run_codegen_fixture(
  "pcm_cache_dot_generated"
  PCM_CACHE ON
  TIMING_JSON "${_header_timing}"
  SOURCES
    "${_src_dir}/alpha_dot_provider.cppm"
    "${_src_dir}/alpha_dot_consumer.cppm")
_gentest_expect_dot_module_cache_state("${_header_timing}" "miss")

file(APPEND "${_src_dir}/alpha_dot_provider.cppm" "\n// source content invalidation sentinel\n")
set(_source_timing "${_generated_dir}/dot_source_timing.json")
_gentest_run_codegen_fixture(
  "pcm_cache_dot_generated"
  PCM_CACHE ON
  TIMING_JSON "${_source_timing}"
  SOURCES
    "${_src_dir}/alpha_dot_provider.cppm"
    "${_src_dir}/alpha_dot_consumer.cppm")
_gentest_expect_dot_module_cache_state("${_source_timing}" "miss")

# A prebuilt-module search directory does not provide the exact selected PCM
# mapping needed for a key. It must bypass the shared cache but still complete
# normal local module precompilation.
set(_prebuilt_module_dir "${_work_dir}/empty-prebuilt-modules")
file(MAKE_DIRECTORY "${_prebuilt_module_dir}")
set(_prebuilt_timing "${_generated_dir}/dot_prebuilt_timing.json")
_gentest_run_codegen_fixture(
  "pcm_cache_dot_generated"
  PCM_CACHE ON
  TIMING_JSON "${_prebuilt_timing}"
  EXTRA_ARGS "-fprebuilt-module-path=${_prebuilt_module_dir}"
  SOURCES
    "${_src_dir}/alpha_dot_provider.cppm"
    "${_src_dir}/alpha_dot_consumer.cppm")
_gentest_expect_dot_module_cache_state("${_prebuilt_timing}" "bypass")

# VFS overlays, PCH inputs, and compiler plugins can alter the module AST
# without appearing in scan-deps' ordinary file-deps closure. Exercise one
# representative semantic side input and require a conservative bypass.
set(_vfs_overlay "${_work_dir}/empty-vfs-overlay.json")
file(WRITE "${_vfs_overlay}" "{\"version\":0,\"roots\":[]}\n")
set(_vfs_timing "${_generated_dir}/dot_vfs_timing.json")
_gentest_run_codegen_fixture(
  "pcm_cache_dot_generated"
  PCM_CACHE ON
  LOG_SCAN_DEPS
  TIMING_JSON "${_vfs_timing}"
  OUTPUT_VARIABLE _vfs_codegen_output
  EXTRA_ARGS "-ivfsoverlay" "${_vfs_overlay}"
  SOURCES
    "${_src_dir}/alpha_dot_provider.cppm"
    "${_src_dir}/alpha_dot_consumer.cppm")
_gentest_expect_dot_module_cache_state("${_vfs_timing}" "bypass")
string(FIND "${_vfs_codegen_output}" "a VFS overlay or source remap is active" _vfs_bypass_diagnostic_pos)
if(_vfs_bypass_diagnostic_pos EQUAL -1)
  message(FATAL_ERROR "VFS-overlay PCM cache bypass did not report the expected reason.\n${_vfs_codegen_output}")
endif()

if(NOT CMAKE_HOST_WIN32)
  find_program(_gentest_sh NAMES sh)
  if(_gentest_sh)
    # Two independent invocations race to publish the same provider and
    # dependent-consumer keys. Readers must only observe a complete directory;
    # losing writers clean their unique temporary directories.
    set(_concurrent_pcm_cache "${_generated_dir}/concurrent_validated_pcm_cache")
    set(_concurrent_output_a "${_generated_dir}/concurrent_a")
    set(_concurrent_output_b "${_generated_dir}/concurrent_b")
    _gentest_concurrent_codegen_command(_concurrent_command_a "pcm_cache_concurrent_a" "${_concurrent_output_a}"
                                        "${_concurrent_pcm_cache}")
    _gentest_concurrent_codegen_command(_concurrent_command_b "pcm_cache_concurrent_b" "${_concurrent_output_b}"
                                        "${_concurrent_pcm_cache}")
    execute_process(
      COMMAND "${_gentest_sh}" -c "${_concurrent_command_a} & ${_concurrent_command_b} & wait"
      WORKING_DIRECTORY "${_work_dir}"
      RESULT_VARIABLE _concurrent_rc
      OUTPUT_VARIABLE _concurrent_out
      ERROR_VARIABLE _concurrent_err
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_STRIP_TRAILING_WHITESPACE)
    if(NOT _concurrent_rc EQUAL 0)
      message(FATAL_ERROR "Concurrent validated PCM cache writers failed.\n--- stdout ---\n${_concurrent_out}\n--- stderr ---\n${_concurrent_err}")
    endif()
    file(GLOB_RECURSE _concurrent_entries LIST_DIRECTORIES FALSE "${_concurrent_pcm_cache}/*/module.pcm")
    list(LENGTH _concurrent_entries _concurrent_entry_count)
    _gentest_expect_equal("${_concurrent_entry_count}" "2" "concurrent validated PCM cache entry count")
    file(GLOB _concurrent_temporary_dirs LIST_DIRECTORIES TRUE "${_concurrent_pcm_cache}/.*.tmp.*")
    list(LENGTH _concurrent_temporary_dirs _concurrent_temporary_dir_count)
    _gentest_expect_equal("${_concurrent_temporary_dir_count}" "0" "concurrent validated PCM cache temporary directory count")
  endif()
endif()

if(NOT CMAKE_HOST_WIN32)
  # An unwritable cache must not make a module build fail or prompt it to
  # replace an existing entry. A changed flag forces a fresh cache key.
  file(CHMOD "${_validated_pcm_cache}" PERMISSIONS OWNER_READ OWNER_EXECUTE)
  set(_readonly_timing "${_generated_dir}/dot_readonly_timing.json")
  _gentest_run_codegen_fixture(
    "pcm_cache_dot_generated"
    PCM_CACHE ON
    TIMING_JSON "${_readonly_timing}"
    EXTRA_ARGS -DPCM_CACHE_READONLY_TEST=1
    SOURCES
      "${_src_dir}/alpha_dot_provider.cppm"
      "${_src_dir}/alpha_dot_consumer.cppm")
  _gentest_expect_dot_module_cache_state("${_readonly_timing}" "miss")
  file(CHMOD "${_validated_pcm_cache}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)

  # Cache-root symlinks are never followed. The ordinary precompile fallback
  # remains usable and the symlink target is not altered.
  set(_symlink_cache "${_generated_dir}/validated_pcm_cache_symlink")
  file(CREATE_LINK "${_validated_pcm_cache}" "${_symlink_cache}" SYMBOLIC)
  set(_symlink_timing "${_generated_dir}/dot_symlink_timing.json")
  _gentest_run_codegen_fixture(
    "pcm_cache_dot_generated"
    PCM_CACHE ON
    PCM_CACHE_DIR "${_symlink_cache}"
    TIMING_JSON "${_symlink_timing}"
    EXTRA_ARGS -DPCM_CACHE_SYMLINK_TEST=1
    SOURCES
      "${_src_dir}/alpha_dot_provider.cppm"
      "${_src_dir}/alpha_dot_consumer.cppm")
  _gentest_expect_dot_module_cache_state("${_symlink_timing}" "miss")
endif()

# The PCM paths are discovered only after module planning. A timing sidecar
# must be rejected before precompilation can remove or replace one of them.
# Subsequent invalidation checks changed the selected shadow header/depfile;
# take a fresh equality baseline before exercising corrupt-cache fallback.
set(_dot_stable_hashes "")
foreach(_dot_output IN LISTS _dot_stable_outputs)
  file(SHA256 "${_dot_output}" _dot_output_hash)
  list(APPEND _dot_stable_hashes "${_dot_output_hash}")
endforeach()
file(GLOB _dot_local_pcm_files LIST_DIRECTORIES FALSE
  "${_generated_dir}/pcm_cache_dot_generated/.gentest_codegen_modules_*/m_*.pcm")
list(SORT _dot_local_pcm_files)
list(LENGTH _dot_local_pcm_files _dot_local_pcm_count)
_gentest_expect_equal("${_dot_local_pcm_count}" "2" "dot-module PCM file count before timing collision")
list(GET _dot_local_pcm_files 0 _dot_timing_collision_pcm)
_gentest_expect_dot_module_timing_collision(
  "timing/generated PCM collision"
  "${_dot_timing_collision_pcm}"
  "generated named-module PCM"
  OFF)

set(_dot_timing_temp_pcm "${_dot_timing_collision_pcm}.tmp")
file(WRITE "${_dot_timing_temp_pcm}" "temporary PCM timing collision sentinel\n")
_gentest_expect_dot_module_timing_collision(
  "timing/temporary PCM collision"
  "${_dot_timing_temp_pcm}"
  "module precompile temporary PCM output"
  OFF)

set(_dot_timing_fallback_pcm "${_build_dir}/alpha_dot_provider.pcm")
file(WRITE "${_dot_timing_fallback_pcm}" "fallback PCM timing collision sentinel\n")
_gentest_expect_dot_module_timing_collision(
  "timing/fallback PCM collision"
  "${_dot_timing_fallback_pcm}"
  "module precompile fallback PCM output"
  OFF)

# Corruption is a safe miss and fallback, never a crash or a changed codegen
# result. Seed a separate cache so the corrupted fixed key is exactly the one
# this repeat invocation will look up. Immutable entries are not repaired.
set(_corrupt_pcm_cache "${_generated_dir}/corrupt_validated_pcm_cache")
set(_dot_corrupt_seed_timing "${_generated_dir}/dot_corrupt_seed_timing.json")
_gentest_run_codegen_fixture(
  "pcm_cache_dot_generated"
  PCM_CACHE ON
  PCM_CACHE_DIR "${_corrupt_pcm_cache}"
  TIMING_JSON "${_dot_corrupt_seed_timing}"
  SOURCES
    "${_src_dir}/alpha_dot_provider.cppm"
    "${_src_dir}/alpha_dot_consumer.cppm")
_gentest_expect_dot_module_cache_state("${_dot_corrupt_seed_timing}" "miss")
file(GLOB_RECURSE _corrupt_cache_entries LIST_DIRECTORIES FALSE "${_corrupt_pcm_cache}/*/module.pcm")
list(SORT _corrupt_cache_entries)
list(LENGTH _corrupt_cache_entries _corrupt_cache_entry_count)
_gentest_expect_equal("${_corrupt_cache_entry_count}" "2" "dedicated corrupt-cache entry count")
foreach(_corrupt_cache_pcm IN LISTS _corrupt_cache_entries)
  file(CHMOD "${_corrupt_cache_pcm}" PERMISSIONS OWNER_READ OWNER_WRITE)
  file(WRITE "${_corrupt_cache_pcm}" "corrupt validated PCM cache entry\n")
endforeach()
set(_dot_corrupt_timing "${_generated_dir}/dot_corrupt_timing.json")
_gentest_run_codegen_fixture(
  "pcm_cache_dot_generated"
  PCM_CACHE ON
  PCM_CACHE_DIR "${_corrupt_pcm_cache}"
  TIMING_JSON "${_dot_corrupt_timing}"
  SOURCES
    "${_src_dir}/alpha_dot_provider.cppm"
    "${_src_dir}/alpha_dot_consumer.cppm")
_gentest_expect_dot_module_cache_state("${_dot_corrupt_timing}" "miss")
foreach(_dot_output _dot_expected_hash IN ZIP_LISTS _dot_stable_outputs _dot_stable_hashes)
  file(SHA256 "${_dot_output}" _dot_actual_hash)
  if(NOT _dot_actual_hash STREQUAL _dot_expected_hash)
    message(FATAL_ERROR "Corrupt validated PCM cache fallback changed '${_dot_output}'")
  endif()
endforeach()

message(STATUS "Run gentest_codegen for the underscore module target...")
_gentest_run_codegen_fixture(
  "pcm_cache_underscore_generated"
  PCM_CACHE ON
  SOURCES
    "${_src_dir}/alpha_underscore_provider.cppm"
    "${_src_dir}/alpha_underscore_consumer.cppm")

if(EXISTS "${_generated_dir}/.gentest_codegen_modules")
  message(FATAL_ERROR "Expected hashed per-target module cache directories, but found legacy shared cache directory '${_generated_dir}/.gentest_codegen_modules'")
endif()

file(GLOB _module_cache_dirs LIST_DIRECTORIES TRUE "${_generated_dir}/*/.gentest_codegen_modules_*")
list(SORT _module_cache_dirs)
list(LENGTH _module_cache_dirs _module_cache_dir_count)
_gentest_expect_equal("${_module_cache_dir_count}" "2" "module cache directory count")

set(_pcm_basenames "")
foreach(_module_cache_dir IN LISTS _module_cache_dirs)
  file(GLOB _local_pcm_files LIST_DIRECTORIES FALSE "${_module_cache_dir}/m_*.pcm")
  list(SORT _local_pcm_files)
  list(LENGTH _local_pcm_files _local_pcm_file_count)
  set(_expected_local_pcm_file_count "1")
  if(_module_cache_dir MATCHES "pcm_cache_dot_generated")
    set(_expected_local_pcm_file_count "2")
  endif()
  _gentest_expect_equal("${_local_pcm_file_count}" "${_expected_local_pcm_file_count}" "local PCM file count in '${_module_cache_dir}'")
  file(GLOB _external_pcm_files LIST_DIRECTORIES FALSE "${_module_cache_dir}/ext_*.pcm")
  list(LENGTH _external_pcm_files _external_pcm_file_count)
  if(_module_cache_dir MATCHES "pcm_cache_underscore_generated" AND _external_pcm_file_count LESS 1)
    message(FATAL_ERROR "Expected external PCM support files in '${_module_cache_dir}', found none")
  endif()
  foreach(_pcm_file IN LISTS _local_pcm_files)
    get_filename_component(_pcm_basename "${_pcm_file}" NAME)
    list(APPEND _pcm_basenames "${_pcm_basename}")
  endforeach()
endforeach()

set(_pcm_basename_unique ${_pcm_basenames})
list(REMOVE_DUPLICATES _pcm_basename_unique)
list(LENGTH _pcm_basenames _pcm_basename_count)
list(LENGTH _pcm_basename_unique _pcm_basename_unique_count)
_gentest_expect_equal("${_pcm_basename_count}" "3" "total PCM basename count")
_gentest_expect_equal("${_pcm_basename_unique_count}" "3" "unique PCM basename count")

message(STATUS "Shared-build-tree PCM cache isolation regression passed")
