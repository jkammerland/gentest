# Requires:
#  -DSOURCE_DIR=<fixture source dir>
#  -DBUILD_ROOT=<path to parent build dir>
#  -DGENTEST_SOURCE_DIR=<path to gentest source tree>
#  -DGENERATOR=<cmake generator name>
# Optional:
#  -DGENERATOR_PLATFORM=<platform>
#  -DGENERATOR_TOOLSET=<toolset>
#  -DTOOLCHAIN_FILE=<toolchain.cmake>
#  -DMAKE_PROGRAM=<path>
#  -DC_COMPILER=<path>
#  -DCXX_COMPILER=<path>
#  -DBUILD_TYPE=<Debug|Release|...>

if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleAutoDiscovery.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleAutoDiscovery.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENERATOR OR "${GENERATOR}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleAutoDiscovery.cmake: GENERATOR not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleAutoDiscovery.cmake: GENTEST_SOURCE_DIR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

set(_work_dir "${BUILD_ROOT}/module_auto_discovery")
set(_src_dir "${_work_dir}/src")
set(_build_dir "${_work_dir}/build")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(COPY "${SOURCE_DIR}/" DESTINATION "${_src_dir}")

gentest_resolve_clang_fixture_compilers(_clang _clangxx)
if(NOT _clang OR NOT _clangxx)
  gentest_skip_test("module auto-discovery regression: clang/clang++ not found")
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
    gentest_skip_test("module auto-discovery regression: ${_supported_ninja_reason}")
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

message(STATUS "Configure module auto-discovery fixture...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${_src_dir}"
    -B "${_build_dir}"
    ${_cmake_cache_args}
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

message(STATUS "Build module auto-discovery fixture...")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --target module_auto_tests
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

set(_exe "${_build_dir}/module_auto_tests")
if(CMAKE_HOST_WIN32)
  set(_exe "${_exe}.exe")
endif()

gentest_check_run_or_fail(
  COMMAND "${_exe}" --list-tests
  WORKING_DIRECTORY "${_build_dir}"
  STRIP_TRAILING_WHITESPACE
  OUTPUT_VARIABLE _list_out)
string(FIND "${_list_out}" "module_auto/basic" _basic_pos)
if(_basic_pos EQUAL -1)
  message(FATAL_ERROR "Expected auto-discovered module test was missing from --list-tests output.\n${_list_out}")
endif()

gentest_check_run_or_fail(
  COMMAND "${_exe}" --run=module_auto/basic
  WORKING_DIRECTORY "${_build_dir}"
  STRIP_TRAILING_WHITESPACE)

gentest_check_run_or_fail(
  COMMAND "${_exe}" --kind=bench --run=module_auto/bench
  WORKING_DIRECTORY "${_build_dir}"
  STRIP_TRAILING_WHITESPACE)

gentest_check_run_or_fail(
  COMMAND "${_exe}" --kind=jitter --run=module_auto/jitter
  WORKING_DIRECTORY "${_build_dir}"
  STRIP_TRAILING_WHITESPACE)

message(STATUS "Module auto-discovery coverage passed")
