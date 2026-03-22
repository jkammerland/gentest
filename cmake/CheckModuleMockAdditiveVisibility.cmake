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
  message(FATAL_ERROR "CheckModuleMockAdditiveVisibility.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleMockAdditiveVisibility.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleMockAdditiveVisibility.cmake: GENTEST_SOURCE_DIR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

set(_work_dir "${BUILD_ROOT}/module_mock_additive_visibility")
set(_src_dir "${_work_dir}/src")
set(_build_dir "${_work_dir}/build")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(COPY "${SOURCE_DIR}/" DESTINATION "${_src_dir}")

gentest_resolve_clang_fixture_compilers(_effective_c_compiler _effective_cxx_compiler)

if(NOT _effective_c_compiler OR NOT _effective_cxx_compiler)
  gentest_skip_test("additive module mock visibility regression: no usable C/C++ compiler pair was provided")
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
  "-DCMAKE_C_COMPILER=${_effective_c_compiler}"
  "-DCMAKE_CXX_COMPILER=${_effective_cxx_compiler}")
if(GENERATOR STREQUAL "Ninja" OR GENERATOR STREQUAL "Ninja Multi-Config")
  gentest_find_supported_ninja(_supported_ninja _supported_ninja_reason)
  if(NOT _supported_ninja)
    gentest_skip_test("additive module mock visibility regression: ${_supported_ninja_reason}")
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
gentest_find_clang_scan_deps(_clang_scan_deps "${_effective_cxx_compiler}")
if(NOT "${_clang_scan_deps}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=${_clang_scan_deps}")
endif()
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()

message(STATUS "Configure additive module mock visibility fixture...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${_src_dir}"
    -B "${_build_dir}"
    ${_cmake_cache_args}
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

message(STATUS "Build additive module mock visibility fixture...")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --target additive_tests
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

set(_generated_dir "${_build_dir}/generated")
set(_header_registry "${_generated_dir}/additive_tests_mock_registry__domain_0000_header.hpp")
set(_provider_registry "${_generated_dir}/additive_tests_mock_registry__domain_0001_gentest_additive_provider.hpp")
set(_provider_wrapper "${_generated_dir}/tu_0001_provider.module.gentest.cppm")
set(_header_consumer_wrapper "${_generated_dir}/tu_0002_header_consumer.module.gentest.cppm")

foreach(_generated_file IN ITEMS
    "${_header_registry}"
    "${_provider_registry}"
    "${_provider_wrapper}"
    "${_header_consumer_wrapper}")
  if(NOT EXISTS "${_generated_file}")
    message(FATAL_ERROR "Expected generated mock artifact was not written: ${_generated_file}")
  endif()
endforeach()

file(READ "${_header_registry}" _header_registry_text)
file(READ "${_provider_registry}" _provider_registry_text)
file(READ "${_provider_wrapper}" _provider_wrapper_text)
file(READ "${_header_consumer_wrapper}" _header_consumer_wrapper_text)

string(FIND "${_header_registry_text}" "shared::Service" _header_pos)
if(_header_pos EQUAL -1)
  message(FATAL_ERROR "Expected header-domain registry to contain shared::Service")
endif()

string(FIND "${_provider_registry_text}" "provider::Service" _provider_pos)
if(_provider_pos EQUAL -1)
  message(FATAL_ERROR "Expected provider module-domain registry to contain provider::Service")
endif()

string(FIND "${_provider_wrapper_text}" "#include <type_traits>" _provider_type_traits_pos)
string(FIND "${_provider_wrapper_text}" "export module gentest.additive_provider;" _provider_module_pos)
if(_provider_type_traits_pos EQUAL -1 OR _provider_module_pos EQUAL -1)
  message(FATAL_ERROR
    "Expected provider module wrapper to contain relocated registration-support includes.\n${_provider_wrapper_text}")
endif()
if(_provider_type_traits_pos GREATER _provider_module_pos)
  message(FATAL_ERROR
    "Expected provider module wrapper to keep <type_traits> in the global module fragment.\n${_provider_wrapper_text}")
endif()

string(FIND "${_header_consumer_wrapper_text}" "#include <type_traits>" _header_consumer_type_traits_pos)
string(FIND "${_header_consumer_wrapper_text}" "export module gentest.additive_header_consumer;" _header_consumer_module_pos)
if(_header_consumer_type_traits_pos EQUAL -1 OR _header_consumer_module_pos EQUAL -1)
  message(FATAL_ERROR
    "Expected header-consumer module wrapper to contain relocated registration-support includes.\n${_header_consumer_wrapper_text}")
endif()
if(_header_consumer_type_traits_pos GREATER _header_consumer_module_pos)
  message(FATAL_ERROR
    "Expected header-consumer module wrapper to keep <type_traits> in the global module fragment.\n${_header_consumer_wrapper_text}")
endif()

string(FIND "${_header_consumer_wrapper_text}" "#include \"gentest/mock_codegen.h\"" _header_consumer_codegen_include_pos)
if(_header_consumer_module_pos EQUAL -1 OR _header_consumer_codegen_include_pos EQUAL -1)
  message(FATAL_ERROR
    "Expected header-consumer module wrapper to contain a relocated mock_codegen include.\n${_header_consumer_wrapper_text}")
endif()
if(_header_consumer_codegen_include_pos GREATER _header_consumer_module_pos)
  message(FATAL_ERROR
    "Expected header-consumer mock_codegen include to live in the global module fragment.\n${_header_consumer_wrapper_text}")
endif()

set(_prog "${_build_dir}/additive_tests${CMAKE_EXECUTABLE_SUFFIX}")
gentest_check_run_or_fail(
  COMMAND "${_prog}" --run=additive/provider_self
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)
gentest_check_run_or_fail(
  COMMAND "${_prog}" --run=additive/header_defined_from_module
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

message(STATUS "Observed additive module mock visibility success")
