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
if("${_clang_scan_deps}" STREQUAL "")
  gentest_skip_test("module PCM cache isolation regression: clang-scan-deps not found")
  return()
endif()
list(APPEND _cmake_cache_args "-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=${_clang_scan_deps}")
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

function(_gentest_run_codegen_fixture output_stem)
  set(multi_value_args SOURCES)
  cmake_parse_arguments(GENTEST "" "" "${multi_value_args}" ${ARGN})

  if(NOT GENTEST_SOURCES)
    message(FATAL_ERROR "_gentest_run_codegen_fixture requires at least one source")
  endif()

  set(_tu_output_dir "${_generated_dir}/${output_stem}")
  set(_mock_registry "${_generated_dir}/${output_stem}_mock_registry.hpp")
  set(_mock_impl "${_generated_dir}/${output_stem}_mock_impl.hpp")
  set(_mock_registry_header_domain "${_generated_dir}/${output_stem}_mock_registry_domain_header.hpp")
  set(_mock_impl_header_domain "${_generated_dir}/${output_stem}_mock_impl_domain_header.hpp")
  set(_mock_registry_module_domain_a "${_generated_dir}/${output_stem}_mock_registry_domain_a.hpp")
  set(_mock_impl_module_domain_a "${_generated_dir}/${output_stem}_mock_impl_domain_a.hpp")
  set(_mock_registry_module_domain_b "${_generated_dir}/${output_stem}_mock_registry_domain_b.hpp")
  set(_mock_impl_module_domain_b "${_generated_dir}/${output_stem}_mock_impl_domain_b.hpp")
  set(_depfile "${_generated_dir}/${output_stem}.gentest.d")
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

  gentest_check_run_or_fail(
    COMMAND
      "${CMAKE_COMMAND}" -E env GENTEST_CODEGEN_KEEP_MODULE_CACHE=1
      "${_codegen_exe}"
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
      --depfile "${_depfile}"
      --compdb "${_build_dir}"
      --clang-scan-deps "${_clang_scan_deps}"
      --host-clang "${_clangxx}"
      --source-root "${_src_dir}"
      ${GENTEST_SOURCES}
      --
      -std=c++20
      -x
      c++-module
      -DGENTEST_CODEGEN=1
    WORKING_DIRECTORY "${_work_dir}"
    STRIP_TRAILING_WHITESPACE)
endfunction()

message(STATUS "Run gentest_codegen for the dot module target...")
_gentest_run_codegen_fixture(
  "pcm_cache_dot_generated"
  SOURCES
    "${_src_dir}/alpha_dot_provider.cppm"
    "${_src_dir}/alpha_dot_consumer.cppm")

message(STATUS "Run gentest_codegen for the underscore module target...")
_gentest_run_codegen_fixture(
  "pcm_cache_underscore_generated"
  SOURCES
    "${_src_dir}/alpha_underscore_provider.cppm"
    "${_src_dir}/alpha_underscore_consumer.cppm")

if(EXISTS "${_generated_dir}/.gentest_codegen_modules")
  message(FATAL_ERROR "Expected hashed per-target module cache directories, but found legacy shared cache directory '${_generated_dir}/.gentest_codegen_modules'")
endif()

file(GLOB _module_cache_dirs LIST_DIRECTORIES TRUE "${_generated_dir}/*/.gentest_codegen_modules-*")
list(SORT _module_cache_dirs)
list(LENGTH _module_cache_dirs _module_cache_dir_count)
_gentest_expect_equal("${_module_cache_dir_count}" "2" "module cache directory count")

set(_pcm_basenames "")
foreach(_module_cache_dir IN LISTS _module_cache_dirs)
  file(GLOB _local_pcm_files LIST_DIRECTORIES FALSE "${_module_cache_dir}/m_*.pcm")
  list(SORT _local_pcm_files)
  list(LENGTH _local_pcm_files _local_pcm_file_count)
  _gentest_expect_equal("${_local_pcm_file_count}" "1" "local PCM file count in '${_module_cache_dir}'")
  file(GLOB _external_pcm_files LIST_DIRECTORIES FALSE "${_module_cache_dir}/ext_*.pcm")
  list(LENGTH _external_pcm_files _external_pcm_file_count)
  if(_external_pcm_file_count LESS 1)
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
_gentest_expect_equal("${_pcm_basename_count}" "2" "total PCM basename count")
_gentest_expect_equal("${_pcm_basename_unique_count}" "2" "unique PCM basename count")

# The mock-inspection step has no --tu-out-dir, so it keeps its PCMs under the working
# directory, which every target of one CMake directory shares. Two targets naming the
# same module at the same source index run that step in parallel; each run needs its
# own cache, and a run that is not asked to keep it leaves nothing behind.
function(_gentest_run_inspect_fixture working_dir manifest_stem)
  gentest_check_run_or_fail(
    COMMAND
      "${CMAKE_COMMAND}" -E env ${ARGN}
      "${_codegen_exe}"
      inspect-mocks
      --mock-manifest-output "${_generated_dir}/${manifest_stem}.mock_manifest.json"
      --compdb "${_build_dir}"
      --clang-scan-deps "${_clang_scan_deps}"
      --host-clang "${_clangxx}"
      --source-root "${_src_dir}"
      "${_src_dir}/alpha_dot_provider.cppm"
      "${_src_dir}/alpha_dot_consumer.cppm"
      --
      -std=c++20
      -x
      c++-module
      -DGENTEST_CODEGEN=1
    WORKING_DIRECTORY "${working_dir}"
    STRIP_TRAILING_WHITESPACE)
endfunction()

set(_inspect_dir "${_work_dir}/inspect")
file(MAKE_DIRECTORY "${_inspect_dir}")
message(STATUS "Run the mock-inspection step for two targets from one working directory...")
_gentest_run_inspect_fixture("${_inspect_dir}" "inspect_first" GENTEST_CODEGEN_KEEP_MODULE_CACHE=1)
_gentest_run_inspect_fixture("${_inspect_dir}" "inspect_second" GENTEST_CODEGEN_KEEP_MODULE_CACHE=1)
file(GLOB _inspect_cache_dirs LIST_DIRECTORIES TRUE "${_inspect_dir}/.gentest_codegen_modules*")
list(LENGTH _inspect_cache_dirs _inspect_cache_dir_count)
_gentest_expect_equal("${_inspect_cache_dir_count}" "2" "mock-inspection module cache directory count")
foreach(_inspect_cache_dir IN LISTS _inspect_cache_dirs)
  file(GLOB _inspect_pcm_files LIST_DIRECTORIES FALSE "${_inspect_cache_dir}/m_*.pcm")
  list(LENGTH _inspect_pcm_files _inspect_pcm_file_count)
  _gentest_expect_equal("${_inspect_pcm_file_count}" "1" "local PCM file count in '${_inspect_cache_dir}'")
endforeach()

set(_cleanup_dir "${_work_dir}/inspect_cleanup")
file(MAKE_DIRECTORY "${_cleanup_dir}")
_gentest_run_inspect_fixture("${_cleanup_dir}" "inspect_cleanup")
file(GLOB _leftover_cache_dirs LIST_DIRECTORIES TRUE "${_cleanup_dir}/.gentest_codegen_modules*")
list(LENGTH _leftover_cache_dirs _leftover_cache_dir_count)
_gentest_expect_equal("${_leftover_cache_dir_count}" "0" "module cache directories left after a run")

# Both consumers import the external module `gentest` with the same flags, so one run
# builds its PCM once and both parses share it.
set(_shared_dir "${_work_dir}/inspect_shared_import")
file(MAKE_DIRECTORY "${_shared_dir}")
execute_process(
  COMMAND
    "${CMAKE_COMMAND}" -E env GENTEST_CODEGEN_LOG_PRECOMPILE=1
    "${_codegen_exe}"
    inspect-mocks
    --mock-manifest-output "${_generated_dir}/inspect_shared_import.mock_manifest.json"
    --compdb "${_build_dir}"
    --clang-scan-deps "${_clang_scan_deps}"
    --host-clang "${_clangxx}"
    --source-root "${_src_dir}"
    "${_src_dir}/alpha_dot_provider.cppm"
    "${_src_dir}/alpha_dot_consumer.cppm"
    "${_src_dir}/alpha_underscore_provider.cppm"
    "${_src_dir}/alpha_underscore_consumer.cppm"
    --
    -std=c++20
    -x
    c++-module
    -DGENTEST_CODEGEN=1
  WORKING_DIRECTORY "${_shared_dir}"
  RESULT_VARIABLE _shared_rc
  OUTPUT_VARIABLE _shared_out
  ERROR_VARIABLE _shared_err)
if(NOT _shared_rc EQUAL 0)
  message(FATAL_ERROR "mock inspection with two importers of 'gentest' failed (${_shared_rc}):\n${_shared_out}\n${_shared_err}")
endif()
string(REGEX MATCHALL "module precompile command for 'gentest':" _gentest_precompiles "${_shared_err}")
list(LENGTH _gentest_precompiles _gentest_precompile_count)
_gentest_expect_equal("${_gentest_precompile_count}" "1" "precompiles of the external module 'gentest' shared by two importers")

message(STATUS "Shared-build-tree PCM cache isolation regression passed")
