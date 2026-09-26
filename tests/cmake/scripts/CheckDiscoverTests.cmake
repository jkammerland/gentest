# Requires:
#  -DSOURCE_DIR=<path to fixture project>
#  -DBUILD_ROOT=<path to parent build dir>
#  -DGENERATOR=<cmake generator name>
# Optional:
#  -DGENERATOR_PLATFORM=<platform>
#  -DGENERATOR_TOOLSET=<toolset>
#  -DTOOLCHAIN_FILE=<toolchain>
#  -DMAKE_PROGRAM=<make/ninja path>
#  -DC_COMPILER=<C compiler>
#  -DCXX_COMPILER=<C++ compiler>
#  -DBUILD_TYPE=<Debug/Release/...>
#  -DBUILD_CONFIG=<Debug/Release/...>   # for multi-config generators

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckDiscoverTests.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT)
  message(FATAL_ERROR "CheckDiscoverTests.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED GENERATOR)
  message(FATAL_ERROR "CheckDiscoverTests.cmake: GENERATOR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")

set(_work_dir "${BUILD_ROOT}/discover_tests")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

set(_build_dir "${_work_dir}/build")

set(_cmake_gen_args -G "${GENERATOR}")
if(DEFINED GENERATOR_PLATFORM AND NOT "${GENERATOR_PLATFORM}" STREQUAL "")
  list(APPEND _cmake_gen_args -A "${GENERATOR_PLATFORM}")
endif()
if(DEFINED GENERATOR_TOOLSET AND NOT "${GENERATOR_TOOLSET}" STREQUAL "")
  list(APPEND _cmake_gen_args -T "${GENERATOR_TOOLSET}")
endif()

set(_cmake_cache_args)
if(DEFINED TOOLCHAIN_FILE AND NOT "${TOOLCHAIN_FILE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}")
endif()
if(DEFINED MAKE_PROGRAM AND NOT "${MAKE_PROGRAM}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()
if(DEFINED C_COMPILER AND NOT "${C_COMPILER}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_C_COMPILER=${C_COMPILER}")
endif()
if(DEFINED CXX_COMPILER AND NOT "${CXX_COMPILER}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}")
endif()
if(DEFINED LLVM_DIR AND NOT "${LLVM_DIR}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DLLVM_DIR=${LLVM_DIR}")
endif()
if(DEFINED Clang_DIR AND NOT "${Clang_DIR}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DClang_DIR=${Clang_DIR}")
endif()
if(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "")
  list(APPEND _cmake_cache_args "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()

set(_ctest_cmd "${CMAKE_CTEST_COMMAND}")
set(_ctest_common_args --output-on-failure)
if(DEFINED BUILD_CONFIG AND NOT "${BUILD_CONFIG}" STREQUAL "")
  list(APPEND _ctest_common_args -C "${BUILD_CONFIG}")
endif()

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
   "demo/quote]==]", "demo/back\\slash$\"", "death/demo/death", "death/demo/death;[",
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
set(_harness "${_build_dir}/GentestCheckDeath.cmake")
function(check_death_result program args expected_message)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DPROG=${program}" "-DARGS=${args}" -P "${_harness}"
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
check_death_result("${_program}" "--run=demo/abort" "Death test passed")

message(STATUS "gentest_discover_tests fixture passed")
