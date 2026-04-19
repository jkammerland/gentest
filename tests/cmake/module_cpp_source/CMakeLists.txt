cmake_minimum_required(VERSION 3.31)
project(gentest_module_cpp_source LANGUAGES CXX)

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

add_executable(cpp_source_module_tests)

target_sources(cpp_source_module_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/cases.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/import_only.cpp)
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/cases.cpp PROPERTIES
    CXX_SCAN_FOR_MODULES ON
    GENTEST_MODULE_NAME gentest.cpp_source_cases)
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/import_only.cpp PROPERTIES
    CXX_SCAN_FOR_MODULES ON)

target_link_libraries(cpp_source_module_tests PRIVATE gentest gentest_main)
target_compile_features(cpp_source_module_tests PRIVATE cxx_std_20)

gentest_attach_codegen(cpp_source_module_tests
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
