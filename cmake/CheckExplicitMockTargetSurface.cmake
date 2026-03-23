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
#  -DBUILD_TYPE=<Debug|Release|...>

if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckExplicitMockTargetSurface.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckExplicitMockTargetSurface.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckExplicitMockTargetSurface.cmake: GENTEST_SOURCE_DIR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

if(GENERATOR MATCHES "Ninja Multi-Config|Visual Studio|Xcode")
  gentest_skip_test("explicit mock target surface regression: explicit mock targets currently require a single-config generator")
  return()
endif()
if(NOT GENERATOR STREQUAL "Ninja")
  gentest_skip_test("explicit mock target surface regression: explicit module mock targets currently require the Ninja generator")
  return()
endif()

if(CMAKE_HOST_WIN32)
  set(_work_dir "${BUILD_ROOT}/emts")
else()
  set(_work_dir "${BUILD_ROOT}/explicit_mock_target_surface")
endif()
set(_src_dir "${_work_dir}/src")
set(_build_dir "${_work_dir}/build")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(COPY "${SOURCE_DIR}/" DESTINATION "${_src_dir}")

gentest_resolve_clang_fixture_compilers(_clang _clangxx)

if(NOT _clang OR NOT _clangxx)
  gentest_skip_test("explicit mock target surface regression: no usable clang/clang++ pair was provided")
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
    gentest_skip_test("explicit mock target surface regression: ${_supported_ninja_reason}")
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
gentest_append_host_apple_sysroot(_cmake_cache_args)

message(STATUS "Configure explicit mock target surface fixture...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${_src_dir}"
    -B "${_build_dir}"
    ${_cmake_cache_args}
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

message(STATUS "Build explicit mock target surface fixture...")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}"
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

foreach(_generated_header IN ITEMS
    "${_build_dir}/generated/header/public/fixture_header_mocks.hpp"
    "${_build_dir}/generated/header_extra/public/fixture_alt_header_mocks.hpp")
  if(NOT EXISTS "${_generated_header}")
    message(FATAL_ERROR "Expected generated explicit mock header was not written: ${_generated_header}")
  endif()
endforeach()

file(GLOB _generated_module_wrappers "${_build_dir}/generated/module/*.module.gentest.cppm")
list(LENGTH _generated_module_wrappers _generated_module_wrapper_count)
if(NOT _generated_module_wrapper_count EQUAL 2)
  message(FATAL_ERROR
    "Expected exactly 2 generated explicit mock module wrappers, found ${_generated_module_wrapper_count}: "
    "${_generated_module_wrappers}")
endif()

set(_generated_module_aggregate "${_build_dir}/generated/module/explicit_module_mocks.cppm")
if(NOT EXISTS "${_generated_module_aggregate}")
  message(FATAL_ERROR "Expected generated explicit mock aggregate module was not written: ${_generated_module_aggregate}")
endif()

foreach(_prog_name IN ITEMS explicit_header_consumer explicit_module_consumer)
  set(_prog_path "${_build_dir}/${_prog_name}${CMAKE_EXECUTABLE_SUFFIX}")
  message(STATUS "Run ${_prog_name}...")
  gentest_check_run_or_fail(
    COMMAND "${_prog_path}"
    WORKING_DIRECTORY "${_work_dir}"
    STRIP_TRAILING_WHITESPACE)
endforeach()

message(STATUS "Mutate local support header and rebuild explicit header consumer...")
file(READ "${_src_dir}/service.hpp" _service_header_content)
string(REPLACE "kServiceSentinel = 9" "kServiceSentinel = 11" _service_header_content "${_service_header_content}")
file(WRITE "${_src_dir}/service.hpp" "${_service_header_content}")

gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}"
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

file(GLOB _staged_service_headers "${_build_dir}/generated/header/defs/deps/*")
set(_saw_updated_staged_service_header FALSE)
foreach(_staged_service_header IN LISTS _staged_service_headers)
  if(IS_DIRECTORY "${_staged_service_header}")
    continue()
  endif()
  file(READ "${_staged_service_header}" _staged_service_header_content)
  if(_staged_service_header_content MATCHES "kServiceSentinel = 11")
    set(_saw_updated_staged_service_header TRUE)
    break()
  endif()
endforeach()
if(NOT _saw_updated_staged_service_header)
  message(FATAL_ERROR
    "Rebuild after editing service.hpp did not refresh any staged explicit mock support header under "
    "'${_build_dir}/generated/header/defs/deps'")
endif()

gentest_check_run_or_fail(
  COMMAND "${_build_dir}/explicit_header_consumer${CMAKE_EXECUTABLE_SUFFIX}"
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)
