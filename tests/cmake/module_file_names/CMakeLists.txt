cmake_minimum_required(VERSION 3.31)
project(gentest_module_file_names LANGUAGES CXX)

# Regression fixture for module file names and layouts:
# - the mock DEFS live directly in the source directory, and the check script
#   builds in a directory inside it, so the DEFS base directory contains the
#   generated wrapper directory;
# - mock.defs.cppm and more.cases.cppm have more than one dot in their names.

if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
    message(FATAL_ERROR "GENTEST_SOURCE_DIR must point at the gentest source tree")
endif()

set(CMAKE_CXX_EXTENSIONS OFF)
set(gentest_BUILD_TESTING OFF CACHE BOOL "" FORCE)
if(DEFINED GENTEST_CODEGEN_EXECUTABLE AND NOT "${GENTEST_CODEGEN_EXECUTABLE}" STREQUAL "")
    set(GENTEST_BUILD_CODEGEN OFF CACHE BOOL "" FORCE)
else()
    set(GENTEST_BUILD_CODEGEN ON CACHE BOOL "" FORCE)
endif()

include("${GENTEST_SOURCE_DIR}/tests/cmake/GentestFixtureDependency.cmake")
gentest_fixture_add_gentest(gentest)

gentest_add_mocks(module_file_names_mocks
    DEFS
        ${CMAKE_CURRENT_SOURCE_DIR}/service.cppm
        ${CMAKE_CURRENT_SOURCE_DIR}/mock.defs.cppm
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/mocks"
    MODULE_NAME module_file_names.service_mocks)

add_executable(module_file_names_tests main.cpp)
target_sources(module_file_names_tests
    PRIVATE
        FILE_SET module_file_names_cases TYPE CXX_MODULES FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/cases.cppm
            ${CMAKE_CURRENT_SOURCE_DIR}/more.cases.cppm)
target_link_libraries(module_file_names_tests PRIVATE gentest::gentest gentest::gentest_runtime module_file_names_mocks)
target_compile_features(module_file_names_tests PRIVATE cxx_std_20)

gentest_attach_codegen(module_file_names_tests
    MODULE_REGISTRATION
    FILE_SET module_file_names_cases
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
