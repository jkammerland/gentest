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
  message(FATAL_ERROR "CheckModuleMockLongDomainOutputs.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleMockLongDomainOutputs.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleMockLongDomainOutputs.cmake: GENTEST_SOURCE_DIR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

set(_work_dir "${BUILD_ROOT}/module_mock_long_domain_outputs")
set(_src_dir "${_work_dir}/src")
set(_build_dir "${_work_dir}/build")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(COPY "${SOURCE_DIR}/" DESTINATION "${_src_dir}")

gentest_resolve_clang_fixture_compilers(_effective_c_compiler _effective_cxx_compiler)
if(NOT _effective_c_compiler OR NOT _effective_cxx_compiler)
  gentest_skip_test("long module mock domain regression: no usable C/C++ compiler pair was provided")
  return()
endif()

if(NOT GENERATOR STREQUAL "Ninja" AND NOT GENERATOR STREQUAL "Ninja Multi-Config")
  gentest_skip_test("long module mock domain regression: build graph inspection requires a Ninja generator")
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
    gentest_skip_test("long module mock domain regression: ${_supported_ninja_reason}")
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

message(STATUS "Configure long module mock domain fixture...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${_src_dir}"
    -B "${_build_dir}"
    ${_cmake_cache_args}
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

message(STATUS "Build long module mock domain fixture...")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --target long_domain_tests
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

set(_build_ninja "${_build_dir}/build.ninja")
if(NOT EXISTS "${_build_ninja}")
  message(FATAL_ERROR "Expected Ninja build file was not written: ${_build_ninja}")
endif()
file(READ "${_build_ninja}" _build_ninja_text)

string(REGEX MATCH "generated/long_domain_tests_mock_registry__domain_0001_[A-Za-z0-9_]+\\.hpp" _expected_registry_rel "${_build_ninja_text}")
if(_expected_registry_rel STREQUAL "")
  message(FATAL_ERROR "Expected build.ninja to declare a long module registry domain output")
endif()
string(REGEX MATCH "generated/long_domain_tests_mock_impl__domain_0001_[A-Za-z0-9_]+\\.hpp" _expected_impl_rel "${_build_ninja_text}")
if(_expected_impl_rel STREQUAL "")
  message(FATAL_ERROR "Expected build.ninja to declare a long module impl domain output")
endif()

set(_expected_registry_output "${_build_dir}/${_expected_registry_rel}")
set(_expected_impl_output "${_build_dir}/${_expected_impl_rel}")

file(GLOB _registry_domain_outputs
  LIST_DIRECTORIES FALSE
  "${_build_dir}/generated/long_domain_tests_mock_registry__domain_0001_*.hpp")
file(GLOB _impl_domain_outputs
  LIST_DIRECTORIES FALSE
  "${_build_dir}/generated/long_domain_tests_mock_impl__domain_0001_*.hpp")

list(LENGTH _registry_domain_outputs _registry_domain_count)
if(NOT _registry_domain_count EQUAL 1)
  message(FATAL_ERROR
    "Expected exactly one long module registry domain output, found ${_registry_domain_count}: ${_registry_domain_outputs}")
endif()
list(LENGTH _impl_domain_outputs _impl_domain_count)
if(NOT _impl_domain_count EQUAL 1)
  message(FATAL_ERROR
    "Expected exactly one long module impl domain output, found ${_impl_domain_count}: ${_impl_domain_outputs}")
endif()

list(GET _registry_domain_outputs 0 _registry_domain_output)
if(NOT EXISTS "${_expected_registry_output}")
  message(FATAL_ERROR
    "CMake declared registry domain output ${_expected_registry_output}, but gentest_codegen wrote ${_registry_domain_output}")
endif()
list(GET _impl_domain_outputs 0 _impl_domain_output)
if(NOT EXISTS "${_expected_impl_output}")
  message(FATAL_ERROR
    "CMake declared impl domain output ${_expected_impl_output}, but gentest_codegen wrote ${_impl_domain_output}")
endif()

file(READ "${_registry_domain_output}" _registry_domain_text)
string(FIND "${_registry_domain_text}" "long_domain::Service" _registry_service_pos)
if(_registry_service_pos EQUAL -1)
  message(FATAL_ERROR
    "Expected long module registry domain output to contain long_domain::Service.\n${_registry_domain_text}")
endif()

set(_prog "${_build_dir}/long_domain_tests${CMAKE_EXECUTABLE_SUFFIX}")
gentest_check_run_or_fail(
  COMMAND "${_prog}" --run=long_domain/provider_self
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

message(STATUS "Observed long module mock domain output success")
