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

set(_work_dir "${BUILD_ROOT}/module_mock_additive_visibility")
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
  set(_effective_c_compiler "${C_COMPILER}")
else()
  unset(_effective_c_compiler)
  find_program(_effective_c_compiler NAMES clang)
endif()

_gentest_is_clang_like(_gentest_has_clang_cxx "${CXX_COMPILER}")
if(DEFINED CXX_COMPILER AND NOT "${CXX_COMPILER}" STREQUAL "" AND _gentest_has_clang_cxx)
  set(_effective_cxx_compiler "${CXX_COMPILER}")
else()
  unset(_effective_cxx_compiler)
  find_program(_effective_cxx_compiler NAMES clang++)
endif()

if(NOT _effective_c_compiler OR NOT _effective_cxx_compiler)
  message(STATUS "Skipping additive module mock visibility regression: no usable C/C++ compiler pair was provided")
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
if(DEFINED TOOLCHAIN_FILE AND NOT "${TOOLCHAIN_FILE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}")
endif()
if(DEFINED MAKE_PROGRAM AND NOT "${MAKE_PROGRAM}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
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

foreach(_generated_file IN ITEMS
    "${_header_registry}"
    "${_provider_registry}")
  if(NOT EXISTS "${_generated_file}")
    message(FATAL_ERROR "Expected generated mock artifact was not written: ${_generated_file}")
  endif()
endforeach()

file(READ "${_header_registry}" _header_registry_text)
file(READ "${_provider_registry}" _provider_registry_text)

string(FIND "${_header_registry_text}" "shared::Service" _header_pos)
if(_header_pos EQUAL -1)
  message(FATAL_ERROR "Expected header-domain registry to contain shared::Service")
endif()

string(FIND "${_provider_registry_text}" "provider::Service" _provider_pos)
if(_provider_pos EQUAL -1)
  message(FATAL_ERROR "Expected provider module-domain registry to contain provider::Service")
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
