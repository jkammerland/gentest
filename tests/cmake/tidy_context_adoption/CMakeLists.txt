cmake_minimum_required(VERSION 3.31)
project(gentest_tidy_context_adoption LANGUAGES C CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPERIMENTAL_CXX_MODULE_DYNDEP 0)

if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
    message(FATAL_ERROR "GENTEST_SOURCE_DIR is required")
endif()

add_subdirectory("${GENTEST_SOURCE_DIR}" gentest-build EXCLUDE_FROM_ALL)

add_library(gentest_tidy_context_bad OBJECT bad_cases.cpp)
target_compile_features(gentest_tidy_context_bad PRIVATE cxx_std_20)
target_link_libraries(gentest_tidy_context_bad PRIVATE gentest::gentest)

add_library(gentest_tidy_context_good OBJECT good_cases.cpp)
target_compile_features(gentest_tidy_context_good PRIVATE cxx_std_20)
target_link_libraries(gentest_tidy_context_good PRIVATE gentest::gentest)
