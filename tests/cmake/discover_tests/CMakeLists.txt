cmake_minimum_required(VERSION 3.31)
project(gentest_discover_tests_fixture LANGUAGES CXX)

include(CTest)
enable_testing()

# Include the in-tree helper under test.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/GentestCodegen.cmake")

add_executable(demo_tests "${CMAKE_CURRENT_LIST_DIR}/main.cpp")
target_compile_features(demo_tests PRIVATE cxx_std_20)

gentest_discover_tests(demo_tests
  DISCOVERY_MODE PRE_TEST
  WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  DEATH_EXPECT_SUBSTRING "fatal path")
