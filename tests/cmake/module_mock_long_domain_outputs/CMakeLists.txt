cmake_minimum_required(VERSION 3.31)
project(gentest_module_mock_long_domain_outputs LANGUAGES CXX)

if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
    message(FATAL_ERROR "GENTEST_SOURCE_DIR must point at the gentest source tree")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(gentest_BUILD_TESTING OFF CACHE BOOL "" FORCE)
if(DEFINED GENTEST_CODEGEN_EXECUTABLE AND NOT "${GENTEST_CODEGEN_EXECUTABLE}" STREQUAL "")
    set(GENTEST_BUILD_CODEGEN OFF CACHE BOOL "" FORCE)
else()
    set(GENTEST_BUILD_CODEGEN ON CACHE BOOL "" FORCE)
endif()

add_subdirectory("${GENTEST_SOURCE_DIR}" gentest)

gentest_add_mocks(long_domain_mocks
    DEFS
        ${CMAKE_CURRENT_SOURCE_DIR}/provider.cppm
        ${CMAKE_CURRENT_SOURCE_DIR}/mock_defs.cppm
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/mocks"
    MODULE_NAME gentest.long_domain_mocks)

add_executable(long_domain_tests
    main.cpp
    consumer.cpp)

gentest_link_mocks(long_domain_tests PRIVATE long_domain_mocks)
target_compile_features(long_domain_tests PRIVATE cxx_std_20)
