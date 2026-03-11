cmake_minimum_required(VERSION 3.31)
project(gentest_mixed_module_mock_registry LANGUAGES CXX)

if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
    message(FATAL_ERROR "GENTEST_SOURCE_DIR must point at the gentest source tree")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(gentest_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(GENTEST_BUILD_CODEGEN ON CACHE BOOL "" FORCE)

add_subdirectory("${GENTEST_SOURCE_DIR}" gentest)

add_executable(mixed_tests
    legacy.cpp)

target_sources(mixed_tests
    PRIVATE
        FILE_SET mixed_cases TYPE CXX_MODULES FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/cases.cppm
            ${CMAKE_CURRENT_SOURCE_DIR}/extra_cases.cppm
            ${CMAKE_CURRENT_SOURCE_DIR}/same_block_cases.cppm)

target_link_libraries(mixed_tests PRIVATE gentest_main gentest_runtime)
target_compile_features(mixed_tests PRIVATE cxx_std_20)
target_include_directories(mixed_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

gentest_attach_codegen(mixed_tests
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
    CLANG_ARGS
        -I${CMAKE_CURRENT_SOURCE_DIR})
