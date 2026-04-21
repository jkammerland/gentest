cmake_minimum_required(VERSION 3.31)
project(gentest_discover_tests_fixture LANGUAGES CXX)

include(CTest)
enable_testing()

# Include the in-tree helper under test.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/GentestCodegen.cmake")

add_executable(demo_tests "${CMAKE_CURRENT_LIST_DIR}/main.cpp")
target_compile_features(demo_tests PRIVATE cxx_std_20)

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
  WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  ${_gentest_extra_args}
  ${_gentest_death_expect_arg} "fatal path")
