# Validates Windows CI configuration and config-aware Windows Debug death-test skip gating.

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckWindowsDebugDeathSkipGate.cmake: SOURCE_DIR not set")
endif()

set(_legacy_gate "WIN32 AND CMAKE_BUILD_TYPE STREQUAL \"Debug\" AND GENTEST_SKIP_WINDOWS_DEBUG_DEATH_TESTS")
set(_workflow_file "${SOURCE_DIR}/.github/workflows/cmake.yml")
set(_files
    "${SOURCE_DIR}/tests/CMakeLists.txt"
    "${SOURCE_DIR}/tests/cmake/Regressions.cmake")

set(_legacy_hits)
set(_missing_config_genex)
foreach(_file IN LISTS _files)
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "Missing file: ${_file}")
  endif()

  file(READ "${_file}" _content)

  string(FIND "${_content}" "${_legacy_gate}" _legacy_pos)
  if(NOT _legacy_pos EQUAL -1)
    list(APPEND _legacy_hits "${_file}")
  endif()

  string(FIND "${_content}" "$<CONFIG:Debug>" _config_genex_pos)
  if(_config_genex_pos EQUAL -1)
    list(APPEND _missing_config_genex "${_file}")
  endif()
endforeach()

if(_legacy_hits)
  string(JOIN "\n  " _hits ${_legacy_hits})
  message(FATAL_ERROR
    "Found legacy Windows Debug skip gate based on CMAKE_BUILD_TYPE (ineffective for multi-config):\n  ${_hits}")
endif()

if(_missing_config_genex)
  string(JOIN "\n  " _missing ${_missing_config_genex})
  message(FATAL_ERROR
    "Expected config-aware skip gate using $<CONFIG:Debug> in:\n  ${_missing}")
endif()

if(NOT EXISTS "${_workflow_file}")
  message(FATAL_ERROR "Missing workflow file: ${_workflow_file}")
endif()

file(READ "${_workflow_file}" _workflow_content)
string(REPLACE "\r\n" "\n" _workflow_content "${_workflow_content}")
string(REGEX MATCH "\n  windows:\n(    [^\n]*\n|\n)*" _windows_job "${_workflow_content}")
if(NOT _windows_job)
  message(FATAL_ERROR "Expected the CMake workflow to define a Windows job.")
endif()

# CI runs RelWithDebInfo for both Clang and MSVC; the opt-in Debug skip gate
# below remains available to local builds and must remain configuration-aware.
foreach(_snippet IN ITEMS
    "-DCMAKE_BUILD_TYPE=RelWithDebInfo"
    "--config RelWithDebInfo"
    [=["-C", "RelWithDebInfo"]=])
  string(REGEX MATCHALL "${_snippet}" _matches "${_windows_job}")
  list(LENGTH _matches _match_count)
  if(NOT _match_count EQUAL 2)
    message(FATAL_ERROR "Expected both Clang and MSVC Windows validation to use '${_snippet}'.")
  endif()
endforeach()

string(FIND "${_windows_job}" "-C RelWithDebInfo" _bazel_ctest_config_pos)
if(_bazel_ctest_config_pos EQUAL -1)
  message(FATAL_ERROR "Expected the Windows Bazel helper CTest to select RelWithDebInfo.")
endif()

string(FIND "${_windows_job}" "GENTEST_SKIP_WINDOWS_DEBUG_DEATH_TESTS" _workflow_skip_pos)
if(NOT _workflow_skip_pos EQUAL -1)
  message(FATAL_ERROR "Windows RelWithDebInfo CI must not opt into Debug death-test skips.")
endif()

string(FIND "${_windows_job}" "-DCMAKE_BUILD_TYPE=Debug" _workflow_debug_pos)
if(NOT _workflow_debug_pos EQUAL -1)
  message(FATAL_ERROR "Windows CI validation must configure RelWithDebInfo.")
endif()

file(READ "${SOURCE_DIR}/tests/CMakeLists.txt" _tests_cmake_content)
string(REGEX MATCH
       "set_tests_properties\\([^)]+PROPERTIES[ \t\r\n]+DISABLED[ \t\r\n]+\"\\$<CONFIG:Debug>\"\\)"
       _windows_debug_skip_gate
       "${_tests_cmake_content}")
if(NOT _windows_debug_skip_gate)
  message(FATAL_ERROR "Expected a Windows Debug death-test skip gate using set_tests_properties(... DISABLED \"$<CONFIG:Debug>\").")
endif()

foreach(_test IN ITEMS
    concurrency_fail_single_death
    concurrency_skip_no_context_death
    concurrency_xfail_no_context_death
    concurrency_multi_noadopt_death
    concurrency_adopted_expect_pass_death
    concurrency_adopted_fmt_expect_pass_death
    concurrency_adopted_expect_fail_death
    concurrency_adopted_assert_death
    concurrency_adopted_expect_throw_death
    concurrency_adopted_fail_death
    concurrency_adopted_skip_death
    concurrency_adopted_xfail_death
    concurrency_adopted_mock_expectation_death
    concurrency_adopted_mock_mode_death
    concurrency_adopted_mock_handle_mutation_death
    concurrency_adopted_mock_closed_context_death
    concurrency_adopted_mock_unexpected_call_death
    concurrency_stop_callback_expect_death
    async_fail_fast_cancel_released_context_aborts)
  string(FIND "${_windows_debug_skip_gate}" "${_test}" _test_pos)
  if(_test_pos EQUAL -1)
    message(FATAL_ERROR
      "Expected Windows Debug death-test skip gate to include '${_test}'.")
  endif()
endforeach()
