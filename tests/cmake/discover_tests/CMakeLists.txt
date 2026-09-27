cmake_minimum_required(VERSION 3.31)
project(gentest_discover_tests_fixture LANGUAGES CXX)

include(CTest)
enable_testing()

# Include the in-tree helper under test.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/GentestCodegen.cmake")

add_executable(demo_tests "${CMAKE_CURRENT_LIST_DIR}/main.cpp")
set_target_properties(demo_tests PROPERTIES OUTPUT_NAME "demo]=]")
target_compile_features(demo_tests PRIVATE cxx_std_20)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/demo_program_$<CONFIG>.txt" CONTENT "$<TARGET_FILE:demo_tests>")

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
  PROPERTIES LABELS "suffix]=")

# Exercise post-build discovery, scalar filters, and empty inventories as well.
gentest_discover_tests(demo_tests
  DISCOVERY_MODE POST_BUILD
  TEST_PREFIX "filtered/"
  TEST_SUFFIX "/suffix"
  TEST_FILTER "demo/*;*"
  EXTRA_ARGS --verify-quoting "--value=]="
  PROPERTIES LABELS "suffix]="
  DEATH_EXPECT_SUBSTRING "fatal path")
gentest_discover_tests(demo_tests
  DISCOVERY_MODE POST_BUILD
  TEST_PREFIX "empty/"
  DISCOVERY_EXTRA_ARGS --empty-inventory)

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/check_list.cmake" [==[
# CTest includes this outside the fixture project's configure-time policy scope.
cmake_policy(SET CMP0057 NEW)
list(LENGTH normal_discovered count)
if(NOT count EQUAL 6)
  message(FATAL_ERROR "Expected six list-representable names, got ${count}: ${normal_discovered}")
endif()
if(NOT "demo/a;b" IN_LIST normal_discovered)
  message(FATAL_ERROR "TEST_LIST must preserve the semicolon-bearing case")
endif()
]==])
set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${CMAKE_CURRENT_BINARY_DIR}/check_list.cmake")
