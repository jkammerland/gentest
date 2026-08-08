cmake_minimum_required(VERSION 3.31)
project(gentest_explicit_mock_timestamp_preservation LANGUAGES CXX)

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

set(_gentest_generated_root "${CMAKE_CURRENT_BINARY_DIR}/generated artifacts with spaces")
gentest_add_mocks(timestamp_mocks
    DEFS "${CMAKE_CURRENT_SOURCE_DIR}/timestamp_mock_defs.hpp"
    OUTPUT_DIR "${_gentest_generated_root}/mock artifacts"
    HEADER_NAME "public headers/timestamp mocks.hpp")

add_executable(timestamp_consumer cases.cpp)
target_compile_features(timestamp_consumer PRIVATE cxx_std_20)
target_include_directories(timestamp_consumer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(timestamp_consumer PRIVATE gentest_main)
gentest_link_mocks(timestamp_consumer PRIVATE timestamp_mocks)
gentest_attach_codegen(timestamp_consumer
    OUTPUT_DIR "${_gentest_generated_root}/consumer artifacts"
    CLANG_ARGS "-I${CMAKE_CURRENT_SOURCE_DIR}")
