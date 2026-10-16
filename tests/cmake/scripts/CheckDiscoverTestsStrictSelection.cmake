# Build each strict-selection policy once, independently of the repeated
# configuration checks for the Windows Debug death-harness gate.
set(_gentest_discover_fixture_name discover_tests_strict_selection)
include("${CMAKE_CURRENT_LIST_DIR}/CheckDiscoverTestsCommon.cmake")

# Both entry paths must reject genuinely empty inventories, not merely an empty
# TEST_LIST: unmatched-bracket names still produce real CTest registrations.
foreach(_mode IN ITEMS POST_BUILD PRE_TEST)
  foreach(_policy IN ITEMS missing empty bracket skipped false_like skipped_death)
    set(_policy_dir "${_work_dir}/strict-${_mode}-${_policy}")
    set(_filter "demo/not-present")
    set(_empty OFF)
    set(_expect_failure TRUE)
    if(_policy STREQUAL "empty")
      set(_empty ON)
      set(_filter "*")
    elseif(_policy STREQUAL "bracket")
      set(_filter "demo/lone[")
      set(_expect_failure FALSE)
    elseif(_policy STREQUAL "skipped")
      set(_filter "demo/skip")
      set(_expect_failure FALSE)
    elseif(_policy STREQUAL "false_like")
      set(_filter "OFF")
    elseif(_policy STREQUAL "skipped_death")
      set(_filter "demo/death_skip")
    endif()
    # Use direct argv here: the unmatched bracket is not a CMake-list value.
    execute_process(
      COMMAND "${CMAKE_COMMAND}" ${_cmake_gen_args} -S "${SOURCE_DIR}" -B "${_policy_dir}"
        ${_cmake_cache_args} "-DGENTEST_DISCOVER_POLICY_FILTER=${_filter}"
        "-DGENTEST_DISCOVER_POLICY_MODE=${_mode}" "-DGENTEST_DISCOVER_POLICY_EMPTY=${_empty}"
      RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
    if(NOT _rc EQUAL 0)
      message(FATAL_ERROR "Strict discovery configure failed: ${_out}\n${_err}")
    endif()
    set(_policy_build --build "${_policy_dir}")
    if(BUILD_CONFIG)
      list(APPEND _policy_build --config "${BUILD_CONFIG}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" ${_policy_build}
      RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
    if(_mode STREQUAL "PRE_TEST")
      if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "Strict discovery fixture build failed: ${_out}\n${_err}")
      endif()
      execute_process(COMMAND "${_ctest_cmd}" --test-dir "${_policy_dir}" --show-only=json-v1 ${_ctest_common_args}
        RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
    endif()
    if(_expect_failure)
      if(_rc EQUAL 0 OR NOT "${_out}\n${_err}" MATCHES "FAIL_ON_EMPTY is enabled")
        message(FATAL_ERROR "Expected strict ${_mode}/${_policy} discovery rejection: ${_rc}\n${_out}\n${_err}")
      endif()
    else()
      if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "Strict ${_mode}/${_policy} must register its match: ${_rc}\n${_out}\n${_err}")
      endif()
      execute_process(COMMAND "${_ctest_cmd}" --test-dir "${_policy_dir}" --show-only=json-v1 ${_ctest_common_args}
        RESULT_VARIABLE _rc OUTPUT_VARIABLE _inventory ERROR_VARIABLE _err)
      if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "Strict discovery inventory failed: ${_err}")
      endif()
      string(JSON _count LENGTH "${_inventory}" tests)
      if(NOT _count EQUAL 1)
        message(FATAL_ERROR "Expected exactly one strict discovery match: ${_inventory}")
      endif()
    endif()
  endforeach()
endforeach()

message(STATUS "gentest_discover_tests strict selection passed")
