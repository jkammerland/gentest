cmake_minimum_required(VERSION 3.31)
project(gentest_explicit_mock_target_validation LANGUAGES CXX)

if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
    message(FATAL_ERROR "GENTEST_SOURCE_DIR must point at the gentest source tree")
endif()
if(NOT DEFINED TEST_CASE OR "${TEST_CASE}" STREQUAL "")
    message(FATAL_ERROR "TEST_CASE must be set")
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

if(TEST_CASE STREQUAL "duplicate_output_dir")
    gentest_add_mocks(explicit_validation_a
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/defs_a.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shared")

    gentest_add_mocks(explicit_validation_b
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/defs_b.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shared")
elseif(TEST_CASE STREQUAL "textual_module_name")
    gentest_add_mocks(explicit_validation_textual_module
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/defs_a.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        MODULE_NAME fixture.validation_textual)
elseif(TEST_CASE STREQUAL "absolute_header_name")
    gentest_add_mocks(explicit_validation_absolute_header
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/defs_a.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        HEADER_NAME "${CMAKE_CURRENT_BINARY_DIR}/absolute.hpp")
elseif(TEST_CASE STREQUAL "escaped_header_name")
    gentest_add_mocks(explicit_validation_escaped_header
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/defs_a.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        HEADER_NAME "../escape.hpp")
elseif(TEST_CASE STREQUAL "reserved_header_name")
    gentest_add_mocks(explicit_validation_reserved_header
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/defs_a.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        HEADER_NAME "explicit_validation_reserved_header_mock_impl.hpp")
elseif(TEST_CASE STREQUAL "generator_expression_defs")
    gentest_add_mocks(explicit_validation_genex_defs
        DEFS
            "$<CONFIG>/defs_a.hpp"
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
elseif(TEST_CASE STREQUAL "generator_expression_output_dir")
    gentest_add_mocks(explicit_validation_genex_output_dir
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/defs_a.hpp
        OUTPUT_DIR "$<CONFIG>/generated")
elseif(TEST_CASE STREQUAL "missing_named_module")
    gentest_add_mocks(explicit_validation_missing_named_module
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/bad_module.cppm
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        MODULE_NAME fixture.validation_missing_named_module)
elseif(TEST_CASE STREQUAL "module_only_header_name")
    gentest_add_mocks(explicit_validation_module_only_header
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/good_module.cppm
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        MODULE_NAME fixture.validation_module_only
        HEADER_NAME "fixture_validation_module_only.hpp")
else()
    message(FATAL_ERROR "Unknown TEST_CASE='${TEST_CASE}'")
endif()
