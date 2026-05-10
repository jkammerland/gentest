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
if(NOT DEFINED REQUIRE_TROMPELOEIL_CONSUMER)
  set(REQUIRE_TROMPELOEIL_CONSUMER OFF)
endif()
if(NOT DEFINED USE_INSTALLED_GENTEST)
  set(USE_INSTALLED_GENTEST OFF)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

if(GENERATOR MATCHES "Ninja Multi-Config|Visual Studio|Xcode")
  gentest_skip_test("third-party mock consumer regression: explicit mock targets currently require a single-config generator")
  return()
endif()

set(_effective_c_compiler "")
if(DEFINED C_COMPILER AND NOT "${C_COMPILER}" STREQUAL "")
  set(_effective_c_compiler "${C_COMPILER}")
endif()
set(_effective_cxx_compiler "")
if(DEFINED CXX_COMPILER AND NOT "${CXX_COMPILER}" STREQUAL "")
  set(_effective_cxx_compiler "${CXX_COMPILER}")
endif()
if("${_effective_c_compiler}" STREQUAL "" OR "${_effective_cxx_compiler}" STREQUAL "")
  gentest_resolve_clang_fixture_compilers(_clang _clangxx)
  if("${_effective_c_compiler}" STREQUAL "")
    set(_effective_c_compiler "${_clang}")
  endif()
  if("${_effective_cxx_compiler}" STREQUAL "")
    set(_effective_cxx_compiler "${_clangxx}")
  endif()
endif()
if("${_effective_c_compiler}" STREQUAL "" OR "${_effective_cxx_compiler}" STREQUAL "")
  gentest_skip_test("third-party mock consumer regression: no usable C/C++ compiler pair was provided")
  return()
endif()

if(CMAKE_HOST_WIN32)
  if(USE_INSTALLED_GENTEST)
    set(_work_dir "${BUILD_ROOT}/tpmc_pkg")
  else()
    set(_work_dir "${BUILD_ROOT}/tpmc")
  endif()
else()
  if(USE_INSTALLED_GENTEST)
    set(_work_dir "${BUILD_ROOT}/third_party_mock_package_consumers")
  else()
    set(_work_dir "${BUILD_ROOT}/third_party_mock_consumers")
  endif()
endif()
set(_src_dir "${_work_dir}/src")
set(_build_dir "${_work_dir}/build")
set(_producer_build_dir "${_work_dir}/producer-build")
set(_install_prefix "${_work_dir}/install")
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
  "-DCMAKE_C_COMPILER=${_effective_c_compiler}"
  "-DCMAKE_CXX_COMPILER=${_effective_cxx_compiler}")
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
if(DEFINED PROG AND NOT "${PROG}" STREQUAL "" AND NOT USE_INSTALLED_GENTEST)
  list(APPEND _cmake_cache_args "-DGENTEST_CODEGEN_EXECUTABLE=${PROG}")
endif()
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()
set(_incoming_prefix_path "")
if(DEFINED CMAKE_PREFIX_PATH AND NOT "${CMAKE_PREFIX_PATH}" STREQUAL "")
  set(_incoming_prefix_path "${CMAKE_PREFIX_PATH}")
endif()
if(NOT "$ENV{CMAKE_PREFIX_PATH}" STREQUAL "")
  if("${_incoming_prefix_path}" STREQUAL "")
    set(_incoming_prefix_path "$ENV{CMAKE_PREFIX_PATH}")
  else()
    string(APPEND _incoming_prefix_path ";$ENV{CMAKE_PREFIX_PATH}")
  endif()
endif()
list(APPEND _cmake_cache_args "-DGENTEST_REQUIRE_REAL_THIRD_PARTY_MOCK_CONSUMERS=${REQUIRE_REAL_CONSUMERS}")
list(APPEND _cmake_cache_args "-DGENTEST_REQUIRE_REAL_TROMPELOEIL_MOCK_CONSUMER=${REQUIRE_TROMPELOEIL_CONSUMER}")
list(APPEND _cmake_cache_args "-DGENTEST_THIRD_PARTY_MOCK_CONSUMERS_USE_PACKAGE=${USE_INSTALLED_GENTEST}")
if(DEFINED GENTEST_CODEGEN_HOST_CLANG AND NOT "${GENTEST_CODEGEN_HOST_CLANG}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DGENTEST_CODEGEN_HOST_CLANG=${GENTEST_CODEGEN_HOST_CLANG}")
elseif(NOT "$ENV{GENTEST_CODEGEN_HOST_CLANG}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DGENTEST_CODEGEN_HOST_CLANG=$ENV{GENTEST_CODEGEN_HOST_CLANG}")
endif()
gentest_append_public_modules_cache_arg(_cmake_cache_args)
set(_clang_scan_deps "")
if(DEFINED CXX_COMPILER_CLANG_SCAN_DEPS
   AND NOT "${CXX_COMPILER_CLANG_SCAN_DEPS}" STREQUAL ""
   AND NOT "${CXX_COMPILER_CLANG_SCAN_DEPS}" MATCHES "-NOTFOUND$")
  set(_clang_scan_deps "${CXX_COMPILER_CLANG_SCAN_DEPS}")
elseif("${_effective_cxx_compiler}" MATCHES "clang(\\+\\+)?([-.][0-9]+)?(\\.exe)?$")
  gentest_find_clang_scan_deps(_clang_scan_deps "${_effective_cxx_compiler}")
endif()
if(NOT "${_clang_scan_deps}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=${_clang_scan_deps}")
endif()
gentest_append_host_apple_sysroot(_cmake_cache_args)

if(USE_INSTALLED_GENTEST)
  message(STATUS "Configure installed gentest producer for third-party mock consumers...")
  gentest_check_run_or_fail(
    COMMAND
      "${CMAKE_COMMAND}"
      ${_cmake_gen_args}
      -S "${GENTEST_SOURCE_DIR}"
      -B "${_producer_build_dir}"
      ${_cmake_cache_args}
      "-Dgentest_INSTALL=ON"
      "-Dgentest_BUILD_TESTING=OFF"
      "-DGENTEST_BUILD_CODEGEN=ON"
      "-DCMAKE_INSTALL_PREFIX=${_install_prefix}"
    WORKING_DIRECTORY "${_work_dir}"
    STRIP_TRAILING_WHITESPACE)

  message(STATUS "Build and install gentest producer for third-party mock consumers...")
  gentest_check_run_or_fail(
    COMMAND "${CMAKE_COMMAND}" --build "${_producer_build_dir}" --target install
    WORKING_DIRECTORY "${_work_dir}"
    STRIP_TRAILING_WHITESPACE)

  set(_consumer_prefix_path "${_install_prefix}")
  if(NOT "${_incoming_prefix_path}" STREQUAL "")
    string(APPEND _consumer_prefix_path ";${_incoming_prefix_path}")
  endif()
else()
  set(_consumer_prefix_path "${_incoming_prefix_path}")
endif()
if(NOT "${_consumer_prefix_path}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_PREFIX_PATH=${_consumer_prefix_path}")
endif()

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

set(_has_gtest_consumer OFF)
set(_has_doctest_consumer OFF)
set(_has_trompeloeil_consumer OFF)
if(EXISTS "${_build_dir}/consumer-stamps/gtest.txt")
  set(_has_gtest_consumer ON)
endif()
if(EXISTS "${_build_dir}/consumer-stamps/doctest.txt")
  set(_has_doctest_consumer ON)
endif()
if(EXISTS "${_build_dir}/consumer-stamps/trompeloeil.txt")
  set(_has_trompeloeil_consumer ON)
endif()

if(NOT _has_gtest_consumer)
  if(REQUIRE_REAL_CONSUMERS)
    message(FATAL_ERROR "third-party mock consumer regression: required GTest/GMock consumer was not configured")
  endif()
  message(STATUS "GTest/GMock generated-mock consumer skipped: GTest/GMock package not found")
endif()
if(NOT _has_doctest_consumer)
  if(REQUIRE_REAL_CONSUMERS)
    message(FATAL_ERROR "third-party mock consumer regression: required doctest consumer was not configured")
  endif()
  message(STATUS "doctest generated-mock consumer skipped: doctest package not found")
endif()
if(NOT _has_trompeloeil_consumer)
  if(REQUIRE_TROMPELOEIL_CONSUMER)
    message(FATAL_ERROR "third-party mock consumer regression: required Trompeloeil consumer was not configured")
  endif()
  message(STATUS "Trompeloeil generated-mock consumer skipped: trompeloeil package not found")
endif()

if(NOT _has_gtest_consumer AND NOT _has_doctest_consumer AND NOT _has_trompeloeil_consumer)
  gentest_skip_test("third-party mock consumer regression: no supported third-party mock packages were found")
  return()
endif()

message(STATUS "Build real third-party mock consumers...")
gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --target real_mock_consumers
  WORKING_DIRECTORY "${_work_dir}"
  STRIP_TRAILING_WHITESPACE)

if(_has_gtest_consumer)
  file(READ "${_build_dir}/consumer-stamps/gtest.txt" _gtest_exe)
  string(STRIP "${_gtest_exe}" _gtest_exe)
  set(_gtest_exe "${_build_dir}/${_gtest_exe}${CMAKE_EXECUTABLE_SUFFIX}")
  message(STATUS "Run ${_gtest_exe}...")
  gentest_check_run_or_fail(
    COMMAND "${_gtest_exe}"
    WORKING_DIRECTORY "${_work_dir}"
    STRIP_TRAILING_WHITESPACE)
endif()

if(_has_doctest_consumer)
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
endif()

if(_has_trompeloeil_consumer)
  file(READ "${_build_dir}/consumer-stamps/trompeloeil.txt" _trompeloeil_exe)
  string(STRIP "${_trompeloeil_exe}" _trompeloeil_exe)
  set(_trompeloeil_exe "${_build_dir}/${_trompeloeil_exe}${CMAKE_EXECUTABLE_SUFFIX}")
  message(STATUS "Run ${_trompeloeil_exe}...")
  gentest_check_run_or_fail(
    COMMAND "${_trompeloeil_exe}"
    WORKING_DIRECTORY "${_work_dir}"
    STRIP_TRAILING_WHITESPACE)
endif()
