cmake_minimum_required(VERSION 3.31)
project(gentest_module_manual_include_whitespace LANGUAGES CXX)

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

add_executable(manual_include_whitespace_tests
    main.cpp)

target_sources(manual_include_whitespace_tests
    PRIVATE
        FILE_SET manual_cases TYPE CXX_MODULES FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/cases.cppm)

target_link_libraries(manual_include_whitespace_tests PRIVATE gentest gentest_runtime)
target_compile_features(manual_include_whitespace_tests PRIVATE cxx_std_20)
target_include_directories(manual_include_whitespace_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

gentest_attach_codegen(manual_include_whitespace_tests
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
