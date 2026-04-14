if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckRunnerRegistryRuntimeHidden.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckRunnerRegistryRuntimeHidden.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED CXX_COMPILER OR "${CXX_COMPILER}" STREQUAL "")
  message(FATAL_ERROR "CheckRunnerRegistryRuntimeHidden.cmake: CXX_COMPILER not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckFixtureWriteHelpers.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

set(_work_dir "${BUILD_ROOT}/runner_registry_runtime_hidden")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

set(_source "${_work_dir}/runner_registry_runtime_hidden.cpp")
gentest_fixture_write_file("${_source}" [=[
#include "gentest/runner.h"

#include <span>

auto main() -> int {
    gentest::detail::register_cases(std::span<const gentest::Case>{});
    static_cast<void>(gentest::detail::snapshot_registered_cases());
    static_cast<void>(gentest::detail::SharedFixtureScope::Suite);
    gentest::detail::register_shared_fixture<int>(gentest::detail::SharedFixtureScope::Suite, "suite", "fixture");
    return 0;
}
]=])

file(TO_CMAKE_PATH "${SOURCE_DIR}" _source_dir_norm)
gentest_make_public_api_compile_args(
  _compile_args
  COMPILER "${CXX_COMPILER}"
  STD "-std=c++20"
  SOURCE_ROOT "${_source_dir_norm}"
  EXTRA_ARGS
    "-c"
    "${_source}"
    "-o"
    "${_work_dir}/runner_registry_runtime_hidden.o")

execute_process(
  COMMAND ${_compile_args}
  WORKING_DIRECTORY "${_work_dir}"
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

if(_rc EQUAL 0)
  message(FATAL_ERROR
    "gentest/runner.h should not expose gentest::detail::register_cases or snapshot_registered_cases.\n"
    "--- stdout ---\n${_out}\n--- stderr ---\n${_err}")
endif()

set(_all_output "${_out}\n${_err}")
if(NOT _all_output MATCHES "register_cases|snapshot_registered_cases|SharedFixtureScope|register_shared_fixture")
  message(FATAL_ERROR
    "Expected compile failure to mention hidden gentest::detail runtime APIs.\n"
    "--- stdout ---\n${_out}\n--- stderr ---\n${_err}")
endif()
