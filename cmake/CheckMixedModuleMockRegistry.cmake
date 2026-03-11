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

set(_work_dir "${BUILD_ROOT}/mixed_module_mock_registry")
set(_src_dir "${_work_dir}/src")
set(_build_dir "${_work_dir}/build")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(COPY "${SOURCE_DIR}/" DESTINATION "${_src_dir}")

function(_gentest_is_clang_like out_var compiler_path)
  if("${compiler_path}" STREQUAL "")
    set(${out_var} FALSE PARENT_SCOPE)
    return()
  endif()
  get_filename_component(_compiler_name "${compiler_path}" NAME)
  if(_compiler_name MATCHES "^clang(\\+\\+)?([-.].*)?$")
    set(${out_var} TRUE PARENT_SCOPE)
  else()
    set(${out_var} FALSE PARENT_SCOPE)
  endif()
endfunction()

_gentest_is_clang_like(_gentest_has_clang_c "${C_COMPILER}")
if(DEFINED C_COMPILER AND NOT "${C_COMPILER}" STREQUAL "" AND _gentest_has_clang_c)
  set(_clang "${C_COMPILER}")
else()
  unset(_clang)
  find_program(_clang NAMES clang)
endif()

_gentest_is_clang_like(_gentest_has_clang_cxx "${CXX_COMPILER}")
if(DEFINED CXX_COMPILER AND NOT "${CXX_COMPILER}" STREQUAL "" AND _gentest_has_clang_cxx)
  set(_clangxx "${CXX_COMPILER}")
else()
  unset(_clangxx)
  find_program(_clangxx NAMES clang++)
endif()

if(NOT _clang OR NOT _clangxx)
  message(STATUS "Skipping mixed module/non-module mock registry regression: no usable clang/clang++ pair was provided")
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
if(DEFINED TOOLCHAIN_FILE AND NOT "${TOOLCHAIN_FILE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}")
endif()
if(DEFINED MAKE_PROGRAM AND NOT "${MAKE_PROGRAM}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
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
set(_header_registry "${_generated_dir}/mixed_tests_mock_registry__domain_0000_header.hpp")
set(_header_impl "${_generated_dir}/mixed_tests_mock_impl__domain_0000_header.hpp")
set(_module_registry "${_generated_dir}/mixed_tests_mock_registry__domain_0001_gentest_mixed_module_cases.hpp")
set(_module_impl "${_generated_dir}/mixed_tests_mock_impl__domain_0001_gentest_mixed_module_cases.hpp")
set(_extra_registry "${_generated_dir}/mixed_tests_mock_registry__domain_0002_gentest_mixed_module_extra_cases.hpp")
set(_extra_impl "${_generated_dir}/mixed_tests_mock_impl__domain_0002_gentest_mixed_module_extra_cases.hpp")
set(_manual_registry "${_generated_dir}/mixed_tests_mock_registry__domain_0003_gentest_mixed_module_manual_include_cases.hpp")
set(_manual_impl "${_generated_dir}/mixed_tests_mock_impl__domain_0003_gentest_mixed_module_manual_include_cases.hpp")
set(_same_block_registry "${_generated_dir}/mixed_tests_mock_registry__domain_0004_gentest_mixed_module_same_block_cases.hpp")
set(_same_block_impl "${_generated_dir}/mixed_tests_mock_impl__domain_0004_gentest_mixed_module_same_block_cases.hpp")

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
    "${_same_block_impl}")
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

string(FIND "${_dispatcher_registry_text}" "mixed_tests_mock_registry__domain_0000_header.hpp" _dispatcher_header_include_pos)
if(_dispatcher_header_include_pos EQUAL -1)
  message(FATAL_ERROR "Expected dispatcher registry to include the header-domain mock shard")
endif()

string(FIND "${_dispatcher_registry_text}" "GENTEST_MOCK_DOMAIN_REGISTRY_PATH" _dispatcher_domain_macro_pos)
if(_dispatcher_domain_macro_pos EQUAL -1)
  message(FATAL_ERROR "Expected dispatcher registry to keep optional source-local mock-domain support")
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
