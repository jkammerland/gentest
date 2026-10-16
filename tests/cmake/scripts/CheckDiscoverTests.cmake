# Core discovery and death-harness checks are also exercised by the
# Windows Debug gate contract. Keep strict-selection builds in their own test.
set(_gentest_discover_fixture_name discover_tests)
include("${CMAKE_CURRENT_LIST_DIR}/CheckDiscoverTestsCommon.cmake")

set(_legacy_expect_build_dir "${_work_dir}/legacy_expect_substring_removed_build")
message(STATUS "Configure gentest_discover_tests removed EXPECT_SUBSTRING fixture...")
execute_process(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${SOURCE_DIR}"
    -B "${_legacy_expect_build_dir}"
    ${_cmake_cache_args}
    -DGENTEST_DISCOVER_TESTS_USE_LEGACY_EXPECT_SUBSTRING=ON
  RESULT_VARIABLE _legacy_expect_configure_rc
  OUTPUT_VARIABLE _legacy_expect_configure_out
  ERROR_VARIABLE _legacy_expect_configure_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE
  WORKING_DIRECTORY "${_work_dir}"
)
set(_legacy_expect_configure_all "${_legacy_expect_configure_out}\n${_legacy_expect_configure_err}")
if(_legacy_expect_configure_rc EQUAL 0)
  message(FATAL_ERROR
    "Expected gentest_discover_tests legacy EXPECT_SUBSTRING configure failure. Output:\n${_legacy_expect_configure_all}")
endif()
string(FIND "${_legacy_expect_configure_all}" "EXPECT_SUBSTRING was removed in gentest 2.0.0" _legacy_expect_removed_pos)
if(_legacy_expect_removed_pos EQUAL -1)
  message(FATAL_ERROR
    "Expected gentest_discover_tests legacy EXPECT_SUBSTRING removal message. Output:\n${_legacy_expect_configure_all}")
endif()

set(_forwarded_legacy_token_build_dir "${_work_dir}/forwarded_expect_substring_token_build")
message(STATUS "Configure gentest_discover_tests forwarded EXPECT_SUBSTRING token fixture...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${SOURCE_DIR}"
    -B "${_forwarded_legacy_token_build_dir}"
    ${_cmake_cache_args}
    -DGENTEST_DISCOVER_TESTS_FORWARD_LEGACY_EXPECT_TOKEN=ON
  STRIP_TRAILING_WHITESPACE
  WORKING_DIRECTORY "${_work_dir}"
  OUTPUT_VARIABLE _forwarded_token_configure_out
)
string(FIND "${_forwarded_token_configure_out}" "EXPECT_SUBSTRING is deprecated" _forwarded_token_warning_pos)
if(NOT _forwarded_token_warning_pos EQUAL -1)
  message(FATAL_ERROR
    "Forwarded EXTRA_ARGS token EXPECT_SUBSTRING must not trigger legacy option handling. Output:\n${_forwarded_token_configure_out}")
endif()

message(STATUS "Configure gentest_discover_tests fixture...")
gentest_check_run_or_fail(
  COMMAND
    "${CMAKE_COMMAND}"
    ${_cmake_gen_args}
    -S "${SOURCE_DIR}"
    -B "${_build_dir}"
    ${_cmake_cache_args}
  STRIP_TRAILING_WHITESPACE
  WORKING_DIRECTORY "${_work_dir}"
)

message(STATUS "Build gentest_discover_tests fixture...")
set(_build_args --build "${_build_dir}")
if(DEFINED BUILD_CONFIG AND NOT "${BUILD_CONFIG}" STREQUAL "")
  list(APPEND _build_args --config "${BUILD_CONFIG}")
endif()
gentest_check_run_or_fail(COMMAND "${CMAKE_COMMAND}" ${_build_args} STRIP_TRAILING_WHITESPACE WORKING_DIRECTORY "${_work_dir}")

message(STATUS "List discovered tests...")
gentest_check_run_or_fail(
  COMMAND "${_ctest_cmd}" --show-only=json-v1 ${_ctest_common_args}
  STRIP_TRAILING_WHITESPACE
  WORKING_DIRECTORY "${_build_dir}"
  OUTPUT_VARIABLE _list_out
)

set(_expected_tests [====[
  ["demo/a", "demo/b", "demo/skip", "demo/has [bracket]", "demo/a;b", "demo/lone[", "demo/lone]",
   "demo/quote]==]", "demo/suffix]=", "demo/nested]=]tail]==", "demo/back\\slash$\"", "death/demo/death", "death/demo/death;[",
   "filtered/demo/a;b/suffix", "filtered/death/demo/death;[/suffix"]
]====])
string(JSON _expected_count LENGTH "${_expected_tests}")
string(JSON _discovered_count LENGTH "${_list_out}" tests)
if(NOT _discovered_count EQUAL _expected_count)
  message(FATAL_ERROR "Expected ${_expected_count} discovered tests, got ${_discovered_count}:\n${_list_out}")
endif()
math(EXPR _last "${_expected_count} - 1")
foreach(_expected_index RANGE 0 ${_last})
  string(JSON _expected GET "${_expected_tests}" ${_expected_index})
  set(_found FALSE)
  foreach(_actual_index RANGE 0 ${_last})
    string(JSON _actual GET "${_list_out}" tests ${_actual_index} name)
    if(_actual STREQUAL _expected)
      set(_found TRUE)
      break()
    endif()
  endforeach()
  if(NOT _found)
    message(FATAL_ERROR "Missing discovered test '${_expected}':\n${_list_out}")
  endif()
endforeach()

message(STATUS "Run discovered tests...")
gentest_check_run_or_fail(
  COMMAND "${_ctest_cmd}" ${_ctest_common_args} -R "^(demo/|filtered/)"
  STRIP_TRAILING_WHITESPACE
  WORKING_DIRECTORY "${_build_dir}"
)

message(STATUS "Run discovered death tests...")
gentest_check_run_or_fail(
  COMMAND "${_ctest_cmd}" -V ${_ctest_common_args} -R "^death/"
  STRIP_TRAILING_WHITESPACE
  WORKING_DIRECTORY "${_build_dir}"
  OUTPUT_VARIABLE _death_out
)
string(FIND "${_death_out}" "Death test passed" _pos)
if(_pos EQUAL -1)
  message(FATAL_ERROR "Expected death harness success message. Output:\n${_death_out}")
endif()

# Exercise the actual generated death harness, including failures before main().
set(_config "${BUILD_CONFIG}")
if(_config STREQUAL "")
  set(_config "${BUILD_TYPE}")
endif()
file(READ "${_build_dir}/demo_program_${_config}.txt" _program)
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")
gentest_read_fixture_emulator(_fixture_emu "${_build_dir}/demo_emulator_${_config}.txt")
set(_harness "${_build_dir}/GentestCheckDeath.cmake")
function(check_death_result program args expected_message)
  # Only the built target uses QEMU; missing/non-executable controls test host launch errors.
  set(_emu_def)
  if("${program}" STREQUAL "${_program}" AND NOT "${_fixture_emu}" STREQUAL "")
    string(REPLACE ";" "\\;" _emu_escaped "${_fixture_emu}")
    set(_emu_def "-DEMU:STRING=${_emu_escaped}")
  endif()
  execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DPROG=${program}" "-DARGS=${args}" ${_emu_def} -P "${_harness}"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
  set(_all "${_out}\n${_err}")
  string(FIND "${_all}" "${expected_message}" _found)
  if(_found EQUAL -1)
    message(FATAL_ERROR "Expected death harness diagnostic '${expected_message}':\n${_all}")
  endif()
  if(expected_message STREQUAL "Death test passed")
    if(NOT _rc EQUAL 0)
      message(FATAL_ERROR "Expected death harness success: ${_rc}\n${_all}")
    endif()
  elseif(_rc EQUAL 0)
    message(FATAL_ERROR "Expected death harness failure: ${_all}")
  endif()
endfunction()
check_death_result("${_build_dir}/missing-executable" "" "process could not execute")
file(WRITE "${_build_dir}/not-executable" "not a program")
check_death_result("${_build_dir}/not-executable" "" "process could not execute")
check_death_result("${_program}" "--run=demo/a" "exit code was 0")
check_death_result("${_program}" "--run=demo/fail" "normal test failure")
check_death_result("${_program}" "--run=demo/death" "Death test passed")
if(WIN32 AND _config STREQUAL "Debug" AND GENTEST_SKIP_WINDOWS_DEBUG_DEATH_TESTS)
  # This fixture invokes the death harness directly, outside the runtime death
  # tests' DISABLED properties. Keep the other discovery/harness checks active.
  # Do not use GENTEST_SKIP_TEST: that would skip the entire enclosing test.
  message(STATUS "GENTEST_KNOWN_SKIP: windows-debug-abort: Application may suspend for debugger attachment.")
else()
  message(STATUS "Run aborting death-harness probe...")
  check_death_result("${_program}" "--run=demo/abort" "Death test passed")
endif()

message(STATUS "gentest_discover_tests fixture passed")
