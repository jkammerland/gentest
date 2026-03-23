cmake_minimum_required(VERSION 3.31)
project(gentest_mixed_module_mock_registry LANGUAGES CXX)

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

gentest_add_mocks(mixed_header_mocks
    DEFS
        ${CMAKE_CURRENT_SOURCE_DIR}/header_mock_defs.hpp
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/header_mocks"
    HEADER_NAME public/mixed_legacy_mocks.hpp)

gentest_add_mocks(mixed_module_mocks
    DEFS
        ${CMAKE_CURRENT_SOURCE_DIR}/cases.cppm
        ${CMAKE_CURRENT_SOURCE_DIR}/extra_cases.cppm
        ${CMAKE_CURRENT_SOURCE_DIR}/same_block_cases.cppm
        ${CMAKE_CURRENT_SOURCE_DIR}/mock_defs.cppm
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/module_mocks"
    MODULE_NAME gentest.mixed_module_mocks)

add_executable(mixed_tests
    main.cpp
    legacy.cpp
    module_consumer.cpp)

gentest_link_mocks(mixed_tests PRIVATE mixed_header_mocks mixed_module_mocks)
target_compile_features(mixed_tests PRIVATE cxx_std_20)
