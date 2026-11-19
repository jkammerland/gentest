cmake_minimum_required(VERSION 3.31)
project(gentest_discover_tests_fixture LANGUAGES CXX)

include(CTest)
enable_testing()

# Include the in-tree helper under test.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/GentestCodegen.cmake")

# The standalone Windows Debug gate also builds this fixture outside the main project.
if(NOT CMAKE_CROSSCOMPILING AND NOT GENTEST_SUPERVISE_EXECUTABLE AND NOT DEFINED GENTEST_DISCOVER_POLICY_FILTER)
  set(_root "${CMAKE_CURRENT_LIST_DIR}/../../..")
  add_library(fixture_notifications STATIC "${_root}/src/notification.cpp")
  add_library(gentest::notifications ALIAS fixture_notifications)
  target_include_directories(fixture_notifications PUBLIC "${_root}/include")
  target_compile_features(fixture_notifications PUBLIC cxx_std_20)
  add_subdirectory("${_root}/tools/supervise" tools/supervise)
endif()

add_executable(demo_tests "${CMAKE_CURRENT_LIST_DIR}/main.cpp")
set_target_properties(demo_tests PROPERTIES OUTPUT_NAME "demo]=]")
set_property(TARGET demo_tests PROPERTY TEST_LAUNCHER "${CMAKE_COMMAND};-E;env;GENTEST_FIXTURE_LAUNCHED=yes;--")
target_compile_features(demo_tests PRIVATE cxx_std_20)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/demo_program_$<CONFIG>.txt" CONTENT "$<TARGET_FILE:demo_tests>")

# Isolated strict-selection variants reuse the same deterministic inventory.
if(DEFINED GENTEST_DISCOVER_POLICY_FILTER)
  set(_policy_extra)
  if(GENTEST_DISCOVER_POLICY_EMPTY)
    list(APPEND _policy_extra DISCOVERY_EXTRA_ARGS --empty-inventory)
  endif()
  gentest_discover_tests(demo_tests
    FAIL_ON_EMPTY
    DISCOVERY_MODE "${GENTEST_DISCOVER_POLICY_MODE}"
    TEST_FILTER "${GENTEST_DISCOVER_POLICY_FILTER}"
    ${_policy_extra})
  return()
endif()

set(_gentest_death_expect_arg DEATH_EXPECT_SUBSTRING)
if(GENTEST_DISCOVER_TESTS_USE_LEGACY_EXPECT_SUBSTRING)
  set(_gentest_death_expect_arg EXPECT_SUBSTRING)
endif()
set(_gentest_extra_args)
if(GENTEST_DISCOVER_TESTS_FORWARD_LEGACY_EXPECT_TOKEN)
  list(APPEND _gentest_extra_args EXTRA_ARGS EXPECT_SUBSTRING)
endif()

gentest_discover_tests(demo_tests
  DISCOVERY_MODE PRE_TEST
  TEST_LIST normal_discovered
  WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  ${_gentest_extra_args}
  ${_gentest_death_expect_arg} "fatal path"
  EXTRA_ARGS --verify-nested-quoting "--nested=]=]tail]=="
  PROPERTIES ENVIRONMENT "GENTEST_FIXTURE_ENV=present" LABELS "suffix]=")

# Exercise post-build discovery, scalar filters, and empty inventories as well.
gentest_discover_tests(demo_tests
  DISCOVERY_MODE POST_BUILD
  TEST_PREFIX "filtered/"
  TEST_SUFFIX "/suffix"
  TEST_FILTER "demo/*;*"
  EXTRA_ARGS --verify-quoting "--value=]="
  PROPERTIES ENVIRONMENT "GENTEST_FIXTURE_ENV=present" LABELS "caller;shared"
  DEATH_EXPECT_SUBSTRING "fatal path")
gentest_discover_tests(demo_tests
  DISCOVERY_MODE POST_BUILD
  TEST_PREFIX "empty/"
  DISCOVERY_EXTRA_ARGS --empty-inventory)

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/check_list.cmake" [==[
# CTest includes this outside the fixture project's configure-time policy scope.
cmake_policy(SET CMP0057 NEW)
list(LENGTH normal_discovered count)
if(NOT count EQUAL 8)
  message(FATAL_ERROR "Expected eight list-representable names, got ${count}: ${normal_discovered}")
endif()
if(NOT "demo/a;b" IN_LIST normal_discovered)
  message(FATAL_ERROR "TEST_LIST must preserve the semicolon-bearing case")
endif()
]==])
set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${CMAKE_CURRENT_BINARY_DIR}/check_list.cmake")
