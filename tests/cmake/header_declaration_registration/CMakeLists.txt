cmake_minimum_required(VERSION 3.31)
project(gentest_header_declaration_registration LANGUAGES CXX)

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

add_executable(header_declaration_registration_tests
    cases_a.cpp
    cases_b.cpp
    cases_c.cpp)
target_link_libraries(header_declaration_registration_tests PRIVATE gentest_main)
target_compile_features(header_declaration_registration_tests PRIVATE cxx_std_20)
set_property(TARGET header_declaration_registration_tests PROPERTY UNITY_BUILD ON)

# Temporary in-tree switch while the old wrapper backend remains the default
# for the wider migration. It is not a user-facing Gentest option.
set_property(TARGET header_declaration_registration_tests PROPERTY GENTEST_INTERNAL_HEADER_DECLARATION_REGISTRATION ON)
gentest_attach_codegen(header_declaration_registration_tests
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
