# Testing framework selection
set(TEST_FRAMEWORK
    "boost-ut"
    CACHE STRING "Testing framework to use")
set_property(CACHE TEST_FRAMEWORK PROPERTY STRINGS boost-ut catch2 gtest doctest)

message(STATUS "Using testing framework: ${TEST_FRAMEWORK}")

# Warn if switching frameworks without cleaning
if(DEFINED CACHE{PREVIOUS_TEST_FRAMEWORK} AND NOT "${PREVIOUS_TEST_FRAMEWORK}" STREQUAL "${TEST_FRAMEWORK}")
  message(WARNING "Test framework changed from ${PREVIOUS_TEST_FRAMEWORK} to ${TEST_FRAMEWORK}. Consider cleaning build directory.")
endif()
set(PREVIOUS_TEST_FRAMEWORK
    "${TEST_FRAMEWORK}"
    CACHE INTERNAL "Previous test framework for change detection")

# Create test executable
add_executable(tests)

# Framework-specific setup
if(TEST_FRAMEWORK STREQUAL "boost-ut")
  # Try to find boost-ut (also known as bext-ut in vcpkg)
  find_package(ut CONFIG QUIET)
  if(NOT ut_FOUND)
    message(STATUS "boost-ut not found via find_package, fetching from GitHub")
    include(FetchContent)
    FetchContent_Declare(
      ut
      GIT_REPOSITORY https://github.com/jkammerland/ut.git
      GIT_TAG master)
    FetchContent_MakeAvailable(ut)
  endif()

  # Create test executable for boost-ut
  target_sources(tests PRIVATE test_boost_ut.cpp)
  target_link_libraries(tests PRIVATE Boost::ut)

elseif(TEST_FRAMEWORK STREQUAL "catch2")
  # Find or fetch Catch2
  find_package(Catch2 3 CONFIG QUIET)
  if(NOT Catch2_FOUND)
    message(STATUS "Catch2 not found, fetching from GitHub")
    include(FetchContent)
    FetchContent_Declare(
      Catch2
      GIT_REPOSITORY https://github.com/catchorg/Catch2.git
      GIT_TAG master)
    FetchContent_MakeAvailable(Catch2)
  endif()

  # Create test executable for Catch2
  target_sources(tests PRIVATE test_catch2.cpp)
  target_link_libraries(tests PRIVATE Catch2::Catch2WithMain)

elseif(TEST_FRAMEWORK STREQUAL "gtest")
  # Find or fetch Google Test
  find_package(GTest CONFIG QUIET)
  if(NOT GTest_FOUND)
    message(STATUS "Google Test not found, fetching from GitHub")
    include(FetchContent)
    FetchContent_Declare(
      googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG master)
    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt
        ON
        CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
  endif()

  # Create test executable for Google Test
  target_sources(tests PRIVATE test_gtest.cpp)
  if(TARGET GTest::gtest_main)
    target_link_libraries(tests PRIVATE GTest::gtest_main)
  else()
    target_link_libraries(tests PRIVATE gtest_main)
  endif()

elseif(TEST_FRAMEWORK STREQUAL "doctest")
  # Find or fetch doctest
  find_package(doctest CONFIG QUIET)
  if(NOT doctest_FOUND)
    message(STATUS "doctest not found, fetching from GitHub")
    include(FetchContent)
    FetchContent_Declare(
      doctest
      GIT_REPOSITORY https://github.com/jkammerland/doctest.git
      GIT_TAG master)
    FetchContent_MakeAvailable(doctest)
  endif()

  # Create test executable for doctest
  target_sources(tests PRIVATE test_doctest.cpp)
  target_link_libraries(tests PRIVATE doctest::doctest)
else()
  message(FATAL_ERROR "Unknown testing framework: ${TEST_FRAMEWORK}")
endif()

# Common test configuration
target_compile_features(tests PRIVATE cxx_std_20)

# Add the test to CTest (enable_testing() must be called in root CMakeLists.txt)
add_test(NAME ${TEST_FRAMEWORK}_tests COMMAND tests)

# Set test properties for better output based on framework
if(TEST_FRAMEWORK STREQUAL "boost-ut")
  set_tests_properties(${TEST_FRAMEWORK}_tests PROPERTIES FAIL_REGULAR_EXPRESSION "FAILED|failed" PASS_REGULAR_EXPRESSION "all tests passed")
elseif(TEST_FRAMEWORK STREQUAL "catch2")
  set_tests_properties(${TEST_FRAMEWORK}_tests PROPERTIES FAIL_REGULAR_EXPRESSION "test cases failed|assertions failed" PASS_REGULAR_EXPRESSION "All tests passed")
elseif(TEST_FRAMEWORK STREQUAL "gtest")
  set_tests_properties(${TEST_FRAMEWORK}_tests PROPERTIES FAIL_REGULAR_EXPRESSION "\\[  FAILED  \\]" PASS_REGULAR_EXPRESSION "\\[  PASSED  \\]")
elseif(TEST_FRAMEWORK STREQUAL "doctest")
  set_tests_properties(${TEST_FRAMEWORK}_tests PROPERTIES FAIL_REGULAR_EXPRESSION "test cases:.*failed" PASS_REGULAR_EXPRESSION "Status: SUCCESS!")
endif()

# Add custom target for running tests with verbose output
add_custom_target(
  run-tests
  COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure --verbose
  DEPENDS tests
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Running tests with ${TEST_FRAMEWORK}")
