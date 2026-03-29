cmake_minimum_required(VERSION 3.31)
project(gentest_module_manual_partial_includes LANGUAGES CXX)

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

gentest_add_mocks(manual_partial_mocks
    DEFS
        ${CMAKE_CURRENT_SOURCE_DIR}/mock_defs.hpp
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/mocks"
    HEADER_NAME public/manual_partial_mocks.hpp)

add_executable(manual_partial_tests)

target_sources(manual_partial_tests
    PRIVATE
        FILE_SET manual_partial_cases TYPE CXX_MODULES FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/registry_cases.cppm
            ${CMAKE_CURRENT_SOURCE_DIR}/impl_cases.cppm)

target_link_libraries(manual_partial_tests PRIVATE gentest gentest_main)
gentest_link_mocks(manual_partial_tests PRIVATE manual_partial_mocks)
target_compile_features(manual_partial_tests PRIVATE cxx_std_20)

gentest_attach_codegen(manual_partial_tests
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
