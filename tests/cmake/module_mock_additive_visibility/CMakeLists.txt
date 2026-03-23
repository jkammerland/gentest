cmake_minimum_required(VERSION 3.31)
project(gentest_module_mock_additive_visibility LANGUAGES CXX)

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

gentest_add_mocks(additive_header_mocks
    DEFS
        ${CMAKE_CURRENT_SOURCE_DIR}/header_mock_defs.hpp
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/header_mocks"
    HEADER_NAME public/additive_header_mocks.hpp)

gentest_add_mocks(additive_provider_mocks
    DEFS
        ${CMAKE_CURRENT_SOURCE_DIR}/provider.cppm
        ${CMAKE_CURRENT_SOURCE_DIR}/module_mock_defs.cppm
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/provider_mocks"
    MODULE_NAME gentest.additive_provider_mocks)

add_executable(additive_tests
    main.cpp
    header_consumer.cpp
    provider_consumer.cpp)

target_link_libraries(additive_tests PRIVATE gentest gentest_runtime)
gentest_link_mocks(additive_tests PRIVATE additive_header_mocks additive_provider_mocks)
target_compile_features(additive_tests PRIVATE cxx_std_20)
