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
  message(FATAL_ERROR "CheckMixedModuleMockRegistry.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckMixedModuleMockRegistry.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckMixedModuleMockRegistry.cmake: GENTEST_SOURCE_DIR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

function(_gentest_find_mock_domain_artifact out_var generated_dir stem kind expected_text)
  file(GLOB _candidates "${generated_dir}/${stem}__domain_*")
  set(_matches "")
  foreach(_candidate IN LISTS _candidates)
    get_filename_component(_candidate_name "${_candidate}" NAME)
    if(NOT _candidate_name MATCHES "^${stem}__domain_[0-9]+_.*\\.${kind}$")
      continue()
    endif()
    file(READ "${_candidate}" _candidate_text)
    string(FIND "${_candidate_text}" "${expected_text}" _candidate_pos)
    if(NOT _candidate_pos EQUAL -1)
      list(APPEND _matches "${_candidate}")
    endif()
  endforeach()

  list(LENGTH _matches _match_count)
  if(NOT _match_count EQUAL 1)
    message(FATAL_ERROR
      "Expected exactly one generated ${stem} domain artifact containing '${expected_text}', found ${_match_count}.\n"
      "Candidates:\n${_candidates}\n"
      "Matches:\n${_matches}")
  endif()

  list(GET _matches 0 _match)
  set(${out_var} "${_match}" PARENT_SCOPE)
endfunction()

set(_work_dir "${BUILD_ROOT}/mixed_module_mock_registry")
set(_src_dir "${_work_dir}/src")
set(_build_dir "${_work_dir}/build")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(COPY "${SOURCE_DIR}/" DESTINATION "${_src_dir}")

gentest_resolve_clang_fixture_compilers(_clang _clangxx)

if(NOT _clang OR NOT _clangxx)
  gentest_skip_test("mixed module/non-module mock registry regression: no usable clang/clang++ pair was provided")
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
    gentest_skip_test("mixed module/non-module mock registry regression: ${_supported_ninja_reason}")
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

message(STATUS "Configure mixed module/non-module mock registry fixture...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${_src_dir}"
    -B "${_build_dir}"
    ${_cmake_cache_args}
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

message(STATUS "Build mixed target with legacy and named-module mocks...")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --target mixed_tests
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

set(_generated_dir "${_build_dir}/generated")
set(_dispatcher_registry "${_generated_dir}/mixed_tests_mock_registry.hpp")
set(_dispatcher_impl "${_generated_dir}/mixed_tests_mock_impl.hpp")
set(_module_wrapper "${_generated_dir}/tu_0001_cases.module.gentest.cppm")
set(_manual_wrapper "${_generated_dir}/tu_0003_manual_include_cases.module.gentest.cppm")
set(_same_block_wrapper "${_generated_dir}/tu_0004_same_block_cases.module.gentest.cppm")

_gentest_find_mock_domain_artifact(_header_registry "${_generated_dir}" "mixed_tests_mock_registry" "hpp" "legacy::Service")
_gentest_find_mock_domain_artifact(_header_impl "${_generated_dir}" "mixed_tests_mock_impl" "hpp" "legacy::Service")
_gentest_find_mock_domain_artifact(_module_registry "${_generated_dir}" "mixed_tests_mock_registry" "hpp" "mixmod::Service")
_gentest_find_mock_domain_artifact(_module_impl "${_generated_dir}" "mixed_tests_mock_impl" "hpp" "mixmod::Service")
_gentest_find_mock_domain_artifact(_extra_registry "${_generated_dir}" "mixed_tests_mock_registry" "hpp" "extramod::Worker")
_gentest_find_mock_domain_artifact(_extra_impl "${_generated_dir}" "mixed_tests_mock_impl" "hpp" "extramod::Worker")
_gentest_find_mock_domain_artifact(_manual_registry "${_generated_dir}" "mixed_tests_mock_registry" "hpp" "manualinclude::Service")
_gentest_find_mock_domain_artifact(_manual_impl "${_generated_dir}" "mixed_tests_mock_impl" "hpp" "manualinclude::Service")
_gentest_find_mock_domain_artifact(_same_block_registry "${_generated_dir}" "mixed_tests_mock_registry" "hpp" "sameblock::Service")
_gentest_find_mock_domain_artifact(_same_block_impl "${_generated_dir}" "mixed_tests_mock_impl" "hpp" "sameblock::Service")

foreach(_generated_file IN ITEMS
    "${_dispatcher_registry}"
    "${_dispatcher_impl}"
    "${_header_registry}"
    "${_header_impl}"
    "${_module_registry}"
    "${_module_impl}"
    "${_extra_registry}"
    "${_extra_impl}"
    "${_manual_registry}"
    "${_manual_impl}"
    "${_same_block_registry}"
    "${_same_block_impl}"
    "${_module_wrapper}"
    "${_manual_wrapper}"
    "${_same_block_wrapper}")
  if(NOT EXISTS "${_generated_file}")
    message(FATAL_ERROR "Expected generated mock artifact was not written: ${_generated_file}")
  endif()
endforeach()

file(READ "${_header_registry}" _header_registry_text)
file(READ "${_module_registry}" _module_registry_text)
file(READ "${_extra_registry}" _extra_registry_text)
file(READ "${_manual_registry}" _manual_registry_text)
file(READ "${_same_block_registry}" _same_block_registry_text)
file(READ "${_dispatcher_registry}" _dispatcher_registry_text)
file(READ "${_module_wrapper}" _module_wrapper_text)
file(READ "${_manual_wrapper}" _manual_wrapper_text)
file(READ "${_same_block_wrapper}" _same_block_wrapper_text)

string(FIND "${_header_registry_text}" "legacy::Service" _header_pos)
if(_header_pos EQUAL -1)
  message(FATAL_ERROR "Expected header-domain registry to contain legacy::Service")
endif()

string(FIND "${_module_registry_text}" "mixmod::Service" _module_pos)
if(_module_pos EQUAL -1)
  message(FATAL_ERROR "Expected module-domain registry to contain mixmod::Service")
endif()

string(FIND "${_extra_registry_text}" "extramod::Worker" _extra_pos)
if(_extra_pos EQUAL -1)
  message(FATAL_ERROR "Expected second module-domain registry to contain extramod::Worker")
endif()

string(FIND "${_manual_registry_text}" "manualinclude::Service" _manual_pos)
if(_manual_pos EQUAL -1)
  message(FATAL_ERROR "Expected manual-include module-domain registry to contain manualinclude::Service")
endif()

string(FIND "${_same_block_registry_text}" "sameblock::Service" _same_block_pos)
if(_same_block_pos EQUAL -1)
  message(FATAL_ERROR "Expected same-block module-domain registry to contain sameblock::Service")
endif()

get_filename_component(_header_registry_name "${_header_registry}" NAME)
string(FIND "${_dispatcher_registry_text}" "${_header_registry_name}" _dispatcher_header_include_pos)
if(_dispatcher_header_include_pos EQUAL -1)
  message(FATAL_ERROR "Expected dispatcher registry to include the header-domain mock shard")
endif()

string(FIND "${_dispatcher_registry_text}" "GENTEST_MOCK_DOMAIN_REGISTRY_PATH" _dispatcher_domain_macro_pos)
if(NOT _dispatcher_domain_macro_pos EQUAL -1)
  message(FATAL_ERROR "Expected dispatcher registry to stop carrying unused source-local mock-domain hooks")
endif()

foreach(_wrapper_text IN ITEMS
    "${_module_wrapper_text}"
    "${_manual_wrapper_text}"
    "${_same_block_wrapper_text}")
  string(REGEX MATCH "export[^\n]*module[^\n]*;" _wrapper_module_decl "${_wrapper_text}")
  if("${_wrapper_module_decl}" STREQUAL "")
    message(FATAL_ERROR
      "Expected mixed module wrapper to keep an export module declaration.\n${_wrapper_text}")
  endif()
  string(FIND "${_wrapper_text}" "${_wrapper_module_decl}" _wrapper_module_decl_pos)
  string(FIND "${_wrapper_text}" "// gentest_codegen: injected registration support includes." _wrapper_reg_support_pos)
  if(NOT _wrapper_reg_support_pos EQUAL -1 AND _wrapper_reg_support_pos GREATER _wrapper_module_decl_pos)
    message(FATAL_ERROR
      "Expected mixed module wrappers to keep injected registration support in the global module fragment.\n${_wrapper_text}")
  endif()
endforeach()

string(FIND "${_manual_wrapper_text}" "export module gentest.mixed_module_manual_include_cases;" _manual_module_pos)
string(FIND "${_manual_wrapper_text}" "#include \"gentest/mock_codegen.h\"" _manual_codegen_include_pos)
if(_manual_module_pos EQUAL -1 OR _manual_codegen_include_pos EQUAL -1)
  message(FATAL_ERROR
    "Expected manual-include wrapper to contain a relocated mock_codegen include.\n${_manual_wrapper_text}")
endif()
if(_manual_codegen_include_pos GREATER _manual_module_pos)
  message(FATAL_ERROR
    "Expected manual-include wrapper to relocate mock_codegen into the global module fragment.\n${_manual_wrapper_text}")
endif()

message(STATUS "Run mixed target acceptance cases...")
set(_prog "${_build_dir}/mixed_tests${CMAKE_EXECUTABLE_SUFFIX}")
gentest_check_run_or_fail(
  COMMAND "${_prog}" --run=mixed/legacy_mock
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)
gentest_check_run_or_fail(
  COMMAND "${_prog}" --run=mixed/module_mock
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)
gentest_check_run_or_fail(
  COMMAND "${_prog}" --run=mixed/extra_module_mock
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)
gentest_check_run_or_fail(
  COMMAND "${_prog}" --run=mixed/manual_include_module_mock
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)
gentest_check_run_or_fail(
  COMMAND "${_prog}" --run=mixed/same_block_module_mock
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

message(STATUS "Observed expected mixed legacy/header and named-module mock success")
