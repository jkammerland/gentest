cmake_minimum_required(VERSION 3.31)
project(gentest_module_first_classic_order LANGUAGES CXX)

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

add_executable(module_first_classic_order_tests)
target_sources(module_first_classic_order_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/provider.cppm
    ${CMAKE_CURRENT_SOURCE_DIR}/cases.cpp)
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/provider.cppm PROPERTIES CXX_SCAN_FOR_MODULES ON)

target_link_libraries(module_first_classic_order_tests PRIVATE gentest gentest_main)
target_compile_features(module_first_classic_order_tests PRIVATE cxx_std_20)

gentest_attach_codegen(module_first_classic_order_tests
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
