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
gentest_make_public_api_include_args(
  _include_args
  SOURCE_ROOT "${_source_dir_norm}")
gentest_make_compile_only_command_args(
  _compile_args
  COMPILER "${CXX_COMPILER}"
  STD "-std=c++20"
  SOURCE "${_source}"
  OBJECT "${_work_dir}/runner_registry_runtime_hidden.o"
  INCLUDE_ARGS ${_include_args})

execute_process(
  COMMAND ${_compile_args}
  WORKING_DIRECTORY "${_work_dir}"
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

set(_all_output "${_out}\n${_err}")
set(_hidden_api_pattern "register_cases|snapshot_registered_cases|SharedFixtureScope|register_shared_fixture")
set(_windows_mode_mismatch_pattern "STL4038|/std:c\\+\\+17|/std:c\\+\\+20|/utf-8")

gentest_is_windows_native_llvm_clang(_is_windows_native_llvm_clang "${CXX_COMPILER}")
if(CMAKE_HOST_WIN32 AND _is_windows_native_llvm_clang AND NOT _rc EQUAL 0 AND NOT _all_output MATCHES "${_hidden_api_pattern}")
  gentest_is_msvc_style_compiler(_is_msvc_style_compiler "${CXX_COMPILER}")
  gentest_normalize_std_flag_for_compiler(_msvc_std "clang-cl" "-std=c++20")
  gentest_normalize_include_args_for_compiler(_msvc_include_args "clang-cl" ${_include_args})
  set(_msvc_compile_args
      "${CXX_COMPILER}")
  if(NOT _is_msvc_style_compiler)
    list(APPEND _msvc_compile_args "--driver-mode=cl")
  endif()
  list(APPEND _msvc_compile_args
      "${_msvc_std}"
      ${_msvc_include_args}
      "/utf-8"
      "/EHsc"
      "/c"
      "${_source}"
      "/Fo${_work_dir}/runner_registry_runtime_hidden.o")
  execute_process(
    COMMAND ${_msvc_compile_args}
    WORKING_DIRECTORY "${_work_dir}"
    RESULT_VARIABLE _msvc_rc
    OUTPUT_VARIABLE _msvc_out
    ERROR_VARIABLE _msvc_err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)

  set(_msvc_all_output "${_msvc_out}\n${_msvc_err}")
  if(_all_output MATCHES "${_windows_mode_mismatch_pattern}" OR _msvc_all_output MATCHES "${_hidden_api_pattern}" OR _msvc_rc EQUAL 0)
    set(_rc "${_msvc_rc}")
    set(_out "${_msvc_out}")
    set(_err "${_msvc_err}")
    set(_all_output "${_msvc_all_output}")
  endif()
endif()

if(_rc EQUAL 0)
  message(FATAL_ERROR
    "gentest/runner.h should not expose gentest::detail::register_cases or snapshot_registered_cases.\n"
    "--- stdout ---\n${_out}\n--- stderr ---\n${_err}")
endif()

if(NOT _all_output MATCHES "${_hidden_api_pattern}")
  message(FATAL_ERROR
    "Expected compile failure to mention hidden gentest::detail runtime APIs.\n"
    "--- stdout ---\n${_out}\n--- stderr ---\n${_err}")
endif()

set(_registry_source "${_work_dir}/registry_run_all_tests_visible.cpp")
gentest_fixture_write_file("${_registry_source}" [=[
#include "gentest/registry.h"

#include <span>

auto main() -> int {
    auto run_all_tests = static_cast<int (*)(std::span<const char *>)>(&gentest::run_all_tests);
    static_cast<void>(run_all_tests);
    return 0;
}
]=])

gentest_make_compile_only_command_args(
  _registry_compile_args
  COMPILER "${CXX_COMPILER}"
  STD "-std=c++20"
  SOURCE "${_registry_source}"
  OBJECT "${_work_dir}/registry_run_all_tests_visible.o"
  INCLUDE_ARGS ${_include_args})

execute_process(
  COMMAND ${_registry_compile_args}
  WORKING_DIRECTORY "${_work_dir}"
  RESULT_VARIABLE _registry_rc
  OUTPUT_VARIABLE _registry_out
  ERROR_VARIABLE _registry_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

set(_registry_all_output "${_registry_out}\n${_registry_err}")
if(CMAKE_HOST_WIN32 AND _is_windows_native_llvm_clang AND NOT _registry_rc EQUAL 0)
  gentest_is_msvc_style_compiler(_is_msvc_style_compiler "${CXX_COMPILER}")
  gentest_normalize_std_flag_for_compiler(_msvc_std "clang-cl" "-std=c++20")
  gentest_normalize_include_args_for_compiler(_msvc_include_args "clang-cl" ${_include_args})
  set(_msvc_compile_args
      "${CXX_COMPILER}")
  if(NOT _is_msvc_style_compiler)
    list(APPEND _msvc_compile_args "--driver-mode=cl")
  endif()
  list(APPEND _msvc_compile_args
      "${_msvc_std}"
      ${_msvc_include_args}
      "/utf-8"
      "/EHsc"
      "/c"
      "${_registry_source}"
      "/Fo${_work_dir}/registry_run_all_tests_visible.o")
  execute_process(
    COMMAND ${_msvc_compile_args}
    WORKING_DIRECTORY "${_work_dir}"
    RESULT_VARIABLE _msvc_rc
    OUTPUT_VARIABLE _msvc_out
    ERROR_VARIABLE _msvc_err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)

  if(_registry_all_output MATCHES "${_windows_mode_mismatch_pattern}" OR _msvc_rc EQUAL 0)
    set(_registry_rc "${_msvc_rc}")
    set(_registry_out "${_msvc_out}")
    set(_registry_err "${_msvc_err}")
  endif()
endif()

if(NOT _registry_rc EQUAL 0)
  message(FATAL_ERROR
    "gentest/registry.h should expose gentest::run_all_tests(std::span<const char *>).\n"
    "--- stdout ---\n${_registry_out}\n--- stderr ---\n${_registry_err}")
endif()
