# Requires:
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

if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckSubprojectConsumerDefaults.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckSubprojectConsumerDefaults.cmake: GENTEST_SOURCE_DIR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")

set(_work_dir "${BUILD_ROOT}/subproject_consumer_defaults")
set(_top_level_build_dir "${_work_dir}/top-level-build")
set(_source_dir "${_work_dir}/consumer")
set(_subproject_build_dir "${_work_dir}/subproject-build")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_source_dir}")

file(TO_CMAKE_PATH "${GENTEST_SOURCE_DIR}" _gentest_source_dir)
file(CONFIGURE OUTPUT "${_source_dir}/CMakeLists.txt" CONTENT [=[
cmake_minimum_required(VERSION 3.31)
project(gentest_subproject_defaults_consumer LANGUAGES CXX)

add_subdirectory("@_gentest_source_dir@" gentest-build)

add_executable(gentest_subproject_defaults_consumer main.cpp)
target_link_libraries(gentest_subproject_defaults_consumer PRIVATE gentest::gentest_runtime)
]=] @ONLY)
file(WRITE "${_source_dir}/main.cpp" [=[
int main() { return 0; }
]=])

set(_cmake_gen_args -G "${GENERATOR}")
if(DEFINED GENERATOR_PLATFORM AND NOT "${GENERATOR_PLATFORM}" STREQUAL "")
  list(APPEND _cmake_gen_args -A "${GENERATOR_PLATFORM}")
endif()
if(DEFINED GENERATOR_TOOLSET AND NOT "${GENERATOR_TOOLSET}" STREQUAL "")
  list(APPEND _cmake_gen_args -T "${GENERATOR_TOOLSET}")
endif()

set(_missing_llvm_dir "${_work_dir}/missing-llvm")
set(_missing_clang_dir "${_work_dir}/missing-clang")
set(_cmake_cache_args
  "-DLLVM_DIR=${_missing_llvm_dir}"
  "-DClang_DIR=${_missing_clang_dir}")

if(DEFINED C_COMPILER AND NOT "${C_COMPILER}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_C_COMPILER=${C_COMPILER}")
endif()
if(DEFINED CXX_COMPILER AND NOT "${CXX_COMPILER}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}")
endif()
if(DEFINED MAKE_PROGRAM AND NOT "${MAKE_PROGRAM}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()
if(DEFINED TOOLCHAIN_FILE AND NOT "${TOOLCHAIN_FILE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}")
endif()
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()

function(_gentest_assert_defaults _cache_file _context)
  if(NOT EXISTS "${_cache_file}")
    message(FATAL_ERROR "Expected configure to produce '${_cache_file}'")
  endif()

  file(STRINGS "${_cache_file}" _testing_default REGEX "^gentest_BUILD_TESTING:BOOL=" LIMIT_COUNT 1)
  if(NOT _testing_default STREQUAL "gentest_BUILD_TESTING:BOOL=OFF")
    message(FATAL_ERROR
      "${_context} must default gentest_BUILD_TESTING=OFF so builds do not pull in repo-only tests.\n"
      "Observed cache entry: '${_testing_default}'")
  endif()

  file(STRINGS "${_cache_file}" _codegen_default REGEX "^GENTEST_BUILD_CODEGEN:BOOL=" LIMIT_COUNT 1)
  if(NOT _codegen_default STREQUAL "GENTEST_BUILD_CODEGEN:BOOL=OFF")
    message(FATAL_ERROR
      "${_context} must default GENTEST_BUILD_CODEGEN=OFF so builds do not depend on host LLVM/Clang tooling or index gentest_codegen by default.\n"
      "Observed cache entry: '${_codegen_default}'")
  endif()

  file(STRINGS "${_cache_file}" _public_modules_default REGEX "^GENTEST_ENABLE_PUBLIC_MODULES:STRING=" LIMIT_COUNT 1)
  if(NOT _public_modules_default STREQUAL "GENTEST_ENABLE_PUBLIC_MODULES:STRING=OFF")
    message(FATAL_ERROR
      "${_context} must default GENTEST_ENABLE_PUBLIC_MODULES=OFF so consumers do not build gentest public module interfaces unless they opt in explicitly.\n"
      "Observed cache entry: '${_public_modules_default}'")
  endif()
endfunction()

message(STATUS "Configure top-level default-off fixture...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${GENTEST_SOURCE_DIR}"
    -B "${_top_level_build_dir}"
    ${_cmake_cache_args}
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

_gentest_assert_defaults("${_top_level_build_dir}/CMakeCache.txt" "Top-level configure")

message(STATUS "Configure consumer subproject fixture...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${_source_dir}"
    -B "${_subproject_build_dir}"
    ${_cmake_cache_args}
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

_gentest_assert_defaults("${_subproject_build_dir}/CMakeCache.txt" "Subproject consumer configure")

message(STATUS "Top-level and subproject consumer configures default gentest testing and codegen off")
