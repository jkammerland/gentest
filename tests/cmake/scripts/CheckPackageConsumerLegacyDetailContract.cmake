if (NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
message(FATAL_ERROR "CheckPackageConsumerLegacyDetailContract.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckPackageConsumerLegacyDetailContract.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED PACKAGE_NAME OR "${PACKAGE_NAME}" STREQUAL "")
  message(FATAL_ERROR "CheckPackageConsumerLegacyDetailContract.cmake: PACKAGE_NAME not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckFixtureWriteHelpers.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")

set(_cmake_generator_args)
if(DEFINED GENERATOR AND NOT GENERATOR STREQUAL "")
  list(APPEND _cmake_generator_args -G "${GENERATOR}")
  if(DEFINED GENERATOR_PLATFORM AND NOT GENERATOR_PLATFORM STREQUAL "")
    list(APPEND _cmake_generator_args -A "${GENERATOR_PLATFORM}")
  endif()
  if(DEFINED GENERATOR_TOOLSET AND NOT GENERATOR_TOOLSET STREQUAL "")
    list(APPEND _cmake_generator_args -T "${GENERATOR_TOOLSET}")
  endif()
endif()

set(_cmake_cache_args
    "-Dgentest_BUILD_TESTING=OFF"
    "-DGENTEST_ENABLE_PACKAGE_TESTS=OFF"
    "-DGENTEST_BUILD_CODEGEN=ON"
    "-Dgentest_INSTALL=ON"
    "-DCMAKE_INSTALL_PREFIX=${BUILD_ROOT}/legacy_detail_fixture_contract/install")

if(DEFINED TOOLCHAIN_FILE AND NOT "${TOOLCHAIN_FILE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}")
endif()
if(DEFINED MAKE_PROGRAM AND NOT "${MAKE_PROGRAM}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()

if(DEFINED PACKAGE_TEST_C_COMPILER AND NOT "${PACKAGE_TEST_C_COMPILER}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_C_COMPILER=${PACKAGE_TEST_C_COMPILER}")
elseif(DEFINED C_COMPILER AND NOT "${C_COMPILER}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_C_COMPILER=${C_COMPILER}")
endif()

if(DEFINED PACKAGE_TEST_CXX_COMPILER AND NOT "${PACKAGE_TEST_CXX_COMPILER}" STREQUAL "")
  set(_effective_cxx_compiler "${PACKAGE_TEST_CXX_COMPILER}")
  list(APPEND _cmake_cache_args "-DCMAKE_CXX_COMPILER=${PACKAGE_TEST_CXX_COMPILER}")
elseif(DEFINED CXX_COMPILER AND NOT "${CXX_COMPILER}" STREQUAL "")
  set(_effective_cxx_compiler "${CXX_COMPILER}")
  list(APPEND _cmake_cache_args "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}")
else()
  set(_effective_cxx_compiler "")
endif()

if(DEFINED LLVM_DIR AND NOT "${LLVM_DIR}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DLLVM_DIR=${LLVM_DIR}")
endif()
if(DEFINED Clang_DIR AND NOT "${Clang_DIR}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DClang_DIR=${Clang_DIR}")
endif()
if(DEFINED PACKAGE_TEST_CXX_COMPILER_CLANG_SCAN_DEPS
   AND NOT "${PACKAGE_TEST_CXX_COMPILER_CLANG_SCAN_DEPS}" STREQUAL "")
  list(APPEND _cmake_cache_args
       "-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=${PACKAGE_TEST_CXX_COMPILER_CLANG_SCAN_DEPS}")
endif()

gentest_resolve_fixture_build_type(_effective_build_type "${_effective_cxx_compiler}" "${BUILD_TYPE}")
if(NOT "${_effective_build_type}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_BUILD_TYPE=${_effective_build_type}")
endif()
gentest_append_windows_native_llvm_cache_args(_cmake_cache_args "${_effective_cxx_compiler}" ${_cmake_cache_args})

set(_work_dir "${BUILD_ROOT}/legacy_detail_fixture_contract")
set(_producer_build_dir "${_work_dir}/producer")
set(_install_prefix "${_work_dir}/install")
set(_consumer_source_dir "${_work_dir}/consumer-src")
set(_consumer_build_dir "${_work_dir}/consumer-build")

file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_consumer_source_dir}")

set(_consumer_cmakelists [=[
cmake_minimum_required(VERSION 3.31)
project(gentest_legacy_detail_fixture_contract_consumer LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)

find_package(@PACKAGE_NAME@ CONFIG REQUIRED)

add_executable(legacy_fixture_detail_contract legacy_fixture_detail_contract.cpp)
target_compile_features(legacy_fixture_detail_contract PRIVATE cxx_std_20)
target_link_libraries(legacy_fixture_detail_contract PRIVATE gentest::gentest_main)

add_executable(legacy_registry_detail_contract legacy_registry_detail_contract.cpp)
target_compile_features(legacy_registry_detail_contract PRIVATE cxx_std_20)
target_link_libraries(legacy_registry_detail_contract PRIVATE gentest::gentest_main)
]=])
string(CONFIGURE "${_consumer_cmakelists}" _consumer_cmakelists @ONLY)
gentest_fixture_write_file("${_consumer_source_dir}/CMakeLists.txt" "${_consumer_cmakelists}")

gentest_fixture_write_file("${_consumer_source_dir}/legacy_fixture_detail_contract.cpp" [=[
#include "gentest/fixture.h"

struct LegacyFixture : gentest::FixtureSetup {
    void setUp() override {}
};

auto main() -> int {
    auto handle = gentest::detail::FixtureHandle<LegacyFixture>::empty();
    (void)handle;
    return 0;
}
]=])

gentest_fixture_write_file("${_consumer_source_dir}/legacy_registry_detail_contract.cpp" [=[
#include "gentest/registry.h"

#include <span>

struct LegacyFixture {};

auto main() -> int {
    gentest::detail::register_shared_fixture<LegacyFixture>(
        gentest::detail::SharedFixtureScope::Suite,
        "legacy",
        "LegacyFixture");
    gentest::Case legacy_case{
        .name             = "legacy/detail/compat",
        .fn               = nullptr,
        .file             = __FILE__,
        .line             = __LINE__,
        .is_benchmark     = false,
        .is_jitter        = false,
        .is_baseline      = false,
        .tags             = {},
        .requirements     = {},
        .skip_reason      = {},
        .should_skip      = false,
        .fixture          = {},
        .fixture_lifetime = gentest::FixtureLifetime::None,
        .suite            = "legacy",
    };
    gentest::detail::register_cases(std::span<const gentest::Case>(&legacy_case, 1));
    static_cast<void>(gentest::detail::snapshot_registered_cases());
    return 0;
}
]=])

gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" ${_cmake_generator_args} -S "${SOURCE_DIR}" -B "${_producer_build_dir}" ${_cmake_cache_args}
  WORKING_DIRECTORY "${_work_dir}"
  DISPLAY_COMMAND "configure producer install")
gentest_assert_windows_native_llvm_cache_args(
  "${_producer_build_dir}" "${_effective_cxx_compiler}" "legacy detail contract producer")

set(_is_multi_config_generator FALSE)
set(_effective_build_config "")
if(DEFINED GENERATOR AND NOT GENERATOR STREQUAL "")
  if(GENERATOR STREQUAL "Ninja Multi-Config" OR GENERATOR MATCHES "^Visual Studio" OR GENERATOR STREQUAL "Xcode")
    set(_is_multi_config_generator TRUE)
    gentest_resolve_fixture_build_type(_effective_build_config "${_effective_cxx_compiler}" "${BUILD_CONFIG}")
  endif()
endif()

if(_is_multi_config_generator)
  set(_install_configs Debug Release)
  if(NOT "${_effective_build_config}" STREQUAL "")
    list(APPEND _install_configs "${_effective_build_config}")
  endif()
  list(REMOVE_DUPLICATES _install_configs)
  foreach(_cfg IN LISTS _install_configs)
    gentest_check_run_or_fail(
      COMMAND "${CMAKE_COMMAND}" --build "${_producer_build_dir}" --target install --config "${_cfg}"
      WORKING_DIRECTORY "${_work_dir}"
      DISPLAY_COMMAND "build and install producer package (${_cfg})")
  endforeach()
else()
  gentest_check_run_or_fail(
    COMMAND "${CMAKE_COMMAND}" --build "${_producer_build_dir}" --target install
    WORKING_DIRECTORY "${_work_dir}"
    DISPLAY_COMMAND "build and install producer package")
endif()

file(GLOB_RECURSE _config_candidates
  LIST_DIRECTORIES FALSE
  "${_install_prefix}/*/${PACKAGE_NAME}Config.cmake"
  "${_install_prefix}/*/*/${PACKAGE_NAME}Config.cmake")
if(NOT _config_candidates)
  message(FATAL_ERROR "Installed package config '${PACKAGE_NAME}Config.cmake' not found under '${_install_prefix}'")
endif()
list(GET _config_candidates 0 _config_file)
get_filename_component(_config_dir "${_config_file}" DIRECTORY)

set(_producer_fmt_dir "")
set(_producer_cache_file "${_producer_build_dir}/CMakeCache.txt")
if(EXISTS "${_producer_cache_file}")
  file(STRINGS "${_producer_cache_file}" _producer_fmt_dir_line REGEX "^fmt_DIR:PATH=" LIMIT_COUNT 1)
  if(_producer_fmt_dir_line)
    list(GET _producer_fmt_dir_line 0 _producer_fmt_dir_line_value)
    string(REGEX REPLACE "^fmt_DIR:PATH=" "" _producer_fmt_dir "${_producer_fmt_dir_line_value}")
  endif()
endif()

set(_consumer_cache_args
    "-DCMAKE_PREFIX_PATH=${_install_prefix}"
    "-D${PACKAGE_NAME}_DIR=${_config_dir}")
list(APPEND _consumer_cache_args ${_cmake_cache_args})
if(NOT _producer_fmt_dir STREQUAL "")
  list(APPEND _consumer_cache_args "-Dfmt_DIR=${_producer_fmt_dir}")
endif()

gentest_check_run_or_fail(
  COMMAND "${CMAKE_COMMAND}" ${_cmake_generator_args} -S "${_consumer_source_dir}" -B "${_consumer_build_dir}" ${_consumer_cache_args}
  WORKING_DIRECTORY "${_work_dir}"
  DISPLAY_COMMAND "configure legacy detail consumer")
gentest_assert_windows_native_llvm_cache_args(
  "${_consumer_build_dir}" "${_effective_cxx_compiler}" "legacy detail contract consumer")

set(_consumer_build_args --build "${_consumer_build_dir}")
if(NOT "${_effective_build_config}" STREQUAL "")
  list(APPEND _consumer_build_args --config "${_effective_build_config}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" ${_consumer_build_args}
  WORKING_DIRECTORY "${_work_dir}"
  RESULT_VARIABLE _build_rc
  OUTPUT_VARIABLE _build_out
  ERROR_VARIABLE _build_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

if(NOT _build_rc EQUAL 0)
  message(FATAL_ERROR
    "Legacy detail contract consumer build should succeed through the compatibility bridge.\n"
    "--- stdout ---\n${_build_out}\n--- stderr ---\n${_build_err}")
endif()

set(_fixture_exe "${_consumer_build_dir}/legacy_fixture_detail_contract")
set(_registry_exe "${_consumer_build_dir}/legacy_registry_detail_contract")
if(CMAKE_HOST_WIN32)
  string(APPEND _fixture_exe ".exe")
  string(APPEND _registry_exe ".exe")
endif()
if(NOT "${_effective_build_config}" STREQUAL "")
  set(_fixture_exe_with_config "${_consumer_build_dir}/${_effective_build_config}/legacy_fixture_detail_contract")
  set(_registry_exe_with_config "${_consumer_build_dir}/${_effective_build_config}/legacy_registry_detail_contract")
  if(CMAKE_HOST_WIN32)
    string(APPEND _fixture_exe_with_config ".exe")
    string(APPEND _registry_exe_with_config ".exe")
  endif()
  if(EXISTS "${_fixture_exe_with_config}")
    set(_fixture_exe "${_fixture_exe_with_config}")
  endif()
  if(EXISTS "${_registry_exe_with_config}")
    set(_registry_exe "${_registry_exe_with_config}")
  endif()
endif()

gentest_check_run_or_fail(
  COMMAND "${_fixture_exe}"
  WORKING_DIRECTORY "${_work_dir}"
  DISPLAY_COMMAND "run legacy fixture detail consumer")
gentest_check_run_or_fail(
  COMMAND "${_registry_exe}"
  WORKING_DIRECTORY "${_work_dir}"
  DISPLAY_COMMAND "run legacy registry detail consumer")
