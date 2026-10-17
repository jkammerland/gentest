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

# This textual compatibility consumer uses the same installed package as the
# include-only smoke consumers; its authored API assertions remain independent.
set(PACKAGE_TEST_USE_MODULES OFF)
include("${CMAKE_CURRENT_LIST_DIR}/PackageFixtureContext.cmake")
gentest_package_fixture_context()
if(NOT EXISTS "${_producer_build_dir}/CMakeCache.txt")
  message(FATAL_ERROR "Legacy detail consumer requires its CTest package producer fixture")
endif()
set(_work_dir "${BUILD_ROOT}/legacy_detail_fixture_contract")
set(_consumer_source_dir "${_work_dir}/consumer-src")
set(_consumer_build_dir "${_work_dir}/consumer-build")

file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_consumer_source_dir}")

set(_consumer_cmakelists [=[
cmake_minimum_required(VERSION 3.31)
project(gentest_legacy_detail_fixture_contract_consumer LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)

find_package(@PACKAGE_NAME@ CONFIG REQUIRED)

function(gentest_add_legacy_detail_contract target source)
    add_executable(${target} ${source})
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_link_libraries(${target} PRIVATE gentest::gentest_main)
endfunction()

gentest_add_legacy_detail_contract(legacy_fixture_detail_contract legacy_fixture_detail_contract.cpp)
gentest_add_legacy_detail_contract(legacy_registry_detail_contract legacy_registry_detail_contract.cpp)
gentest_add_legacy_detail_contract(detail_generated_runtime_contract detail_generated_runtime_contract.cpp)
gentest_add_legacy_detail_contract(detail_registration_runtime_contract detail_registration_runtime_contract.cpp)
gentest_add_legacy_detail_contract(detail_fixture_runtime_contract detail_fixture_runtime_contract.cpp)
gentest_add_legacy_detail_contract(detail_registry_runtime_contract detail_registry_runtime_contract.cpp)
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

gentest_fixture_write_file("${_consumer_source_dir}/detail_generated_runtime_contract.cpp" [=[
#include "gentest/detail/generated_runtime.h"

#include <span>

struct DirectGeneratedFixture {};

auto main() -> int {
    auto handle = gentest::detail::FixtureHandle<DirectGeneratedFixture>::empty();
    (void)handle;
    gentest::detail::register_shared_fixture<DirectGeneratedFixture>(
        gentest::detail::SharedFixtureScope::Suite,
        "direct",
        "DirectGeneratedFixture");
    gentest::Case generated_case{
        .name             = "direct/generated-runtime",
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
        .suite            = "direct",
    };
    gentest::detail::register_cases(std::span<const gentest::Case>(&generated_case, 1));
    return 0;
}
]=])

gentest_fixture_write_file("${_consumer_source_dir}/detail_registration_runtime_contract.cpp" [=[
#include "gentest/detail/registration_runtime.h"

#include <span>

auto main() -> int {
    gentest::Case registration_case{
        .name             = "direct/registration-runtime",
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
        .suite            = "direct",
    };
    gentest::detail::register_cases(std::span<const gentest::Case>(&registration_case, 1));
    return 0;
}
]=])

gentest_fixture_write_file("${_consumer_source_dir}/detail_fixture_runtime_contract.cpp" [=[
#include "gentest/detail/fixture_runtime.h"

struct DirectFixture {};

auto main() -> int {
    auto handle = gentest::detail::FixtureHandle<DirectFixture>::empty();
    auto *setup_shared_fixtures = &gentest::detail::setup_shared_fixtures;
    auto *teardown_shared_fixtures = &gentest::detail::teardown_shared_fixtures;
    (void)handle;
    (void)setup_shared_fixtures;
    (void)teardown_shared_fixtures;
    return 0;
}
]=])

gentest_fixture_write_file("${_consumer_source_dir}/detail_registry_runtime_contract.cpp" [=[
#include "gentest/detail/registry_runtime.h"

auto main() -> int {
    static_cast<void>(gentest::detail::snapshot_registered_cases());
    return 0;
}
]=])

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

set(_consumer_targets
    legacy_fixture_detail_contract
    legacy_registry_detail_contract
    detail_generated_runtime_contract
    detail_registration_runtime_contract
    detail_fixture_runtime_contract
    detail_registry_runtime_contract)

foreach(_consumer_target IN LISTS _consumer_targets)
  set(_consumer_exe "${_consumer_build_dir}/${_consumer_target}")
  if(CMAKE_HOST_WIN32)
    string(APPEND _consumer_exe ".exe")
  endif()
  if(NOT "${_effective_build_config}" STREQUAL "")
    set(_consumer_exe_with_config "${_consumer_build_dir}/${_effective_build_config}/${_consumer_target}")
    if(CMAKE_HOST_WIN32)
      string(APPEND _consumer_exe_with_config ".exe")
    endif()
    if(EXISTS "${_consumer_exe_with_config}")
      set(_consumer_exe "${_consumer_exe_with_config}")
    endif()
  endif()

  gentest_check_run_or_fail(
    COMMAND "${_consumer_exe}"
    WORKING_DIRECTORY "${_work_dir}"
    DISPLAY_COMMAND "run ${_consumer_target}")
endforeach()

gentest_remove_fixture_path("${_work_dir}")
