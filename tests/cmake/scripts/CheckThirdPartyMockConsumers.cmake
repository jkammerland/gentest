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
  message(FATAL_ERROR "CheckThirdPartyMockConsumers.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckThirdPartyMockConsumers.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckThirdPartyMockConsumers.cmake: GENTEST_SOURCE_DIR not set")
endif()
if(NOT DEFINED REQUIRE_REAL_CONSUMERS)
  set(REQUIRE_REAL_CONSUMERS OFF)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

if(GENERATOR MATCHES "Ninja Multi-Config|Visual Studio|Xcode")
  gentest_skip_test("third-party mock consumer regression: explicit mock targets currently require a single-config generator")
  return()
endif()

gentest_resolve_clang_fixture_compilers(_clang _clangxx)
if(NOT _clang OR NOT _clangxx)
  gentest_skip_test("third-party mock consumer regression: no usable clang/clang++ pair was provided")
  return()
endif()

if(CMAKE_HOST_WIN32)
  set(_work_dir "${BUILD_ROOT}/tpmc")
else()
  set(_work_dir "${BUILD_ROOT}/third_party_mock_consumers")
endif()
set(_src_dir "${_work_dir}/src")
set(_build_dir "${_work_dir}/build")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(COPY "${SOURCE_DIR}/" DESTINATION "${_src_dir}")

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
    gentest_skip_test("third-party mock consumer regression: ${_supported_ninja_reason}")
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
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()
if(DEFINED CMAKE_PREFIX_PATH AND NOT "${CMAKE_PREFIX_PATH}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}")
endif()
list(APPEND _cmake_cache_args "-DGENTEST_REQUIRE_REAL_THIRD_PARTY_MOCK_CONSUMERS=${REQUIRE_REAL_CONSUMERS}")
gentest_append_public_modules_cache_arg(_cmake_cache_args)
gentest_find_clang_scan_deps(_clang_scan_deps "${_clangxx}")
if(NOT "${_clang_scan_deps}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=${_clang_scan_deps}")
endif()
gentest_append_host_apple_sysroot(_cmake_cache_args)

message(STATUS "Configure real third-party mock consumers...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${_src_dir}"
    -B "${_build_dir}"
    ${_cmake_cache_args}
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

if(NOT EXISTS "${_build_dir}/consumer-stamps/gtest.txt")
  if(REQUIRE_REAL_CONSUMERS)
    message(FATAL_ERROR "third-party mock consumer regression: required GTest/GMock consumer was not configured")
  endif()
  gentest_skip_test("third-party mock consumer regression: GTest/GMock package not found")
  return()
endif()

message(STATUS "Build real third-party mock consumers...")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --target real_mock_consumers
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

file(READ "${_build_dir}/consumer-stamps/gtest.txt" _gtest_exe)
string(STRIP "${_gtest_exe}" _gtest_exe)
set(_gtest_exe "${_build_dir}/${_gtest_exe}${CMAKE_EXECUTABLE_SUFFIX}")
message(STATUS "Run ${_gtest_exe}...")
gentest_check_run_or_fail(
  COMMAND "${_gtest_exe}"
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

if(EXISTS "${_build_dir}/consumer-stamps/doctest.txt")
  file(READ "${_build_dir}/consumer-stamps/doctest.txt" _doctest_exe)
  string(STRIP "${_doctest_exe}" _doctest_exe)
  set(_doctest_exe "${_build_dir}/${_doctest_exe}${CMAKE_EXECUTABLE_SUFFIX}")
  message(STATUS "Run ${_doctest_exe}...")
  gentest_check_run_or_fail(
    COMMAND "${_doctest_exe}"
    WORKING_DIRECTORY "${_work_dir}"
    STRIP_TRAILING_WHITESPACE)
  message(STATUS "Run ${_doctest_exe} --gentest-probe-gmock-failure...")
  execute_process(
    COMMAND "${_doctest_exe}" "--gentest-probe-gmock-failure"
    WORKING_DIRECTORY "${_work_dir}"
    RESULT_VARIABLE _doctest_probe_rc
    OUTPUT_VARIABLE _doctest_probe_out
    ERROR_VARIABLE _doctest_probe_err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)
  if(_doctest_probe_rc EQUAL 0)
    message(FATAL_ERROR
      "Expected doctest generated-mock consumer to propagate GMock failures.\n"
      "--- stdout ---\n${_doctest_probe_out}\n--- stderr ---\n${_doctest_probe_err}")
  endif()
else()
  if(REQUIRE_REAL_CONSUMERS)
    message(FATAL_ERROR "third-party mock consumer regression: required doctest consumer was not configured")
  endif()
  message(STATUS "doctest generated-mock consumer skipped: doctest package not found")
endif()
