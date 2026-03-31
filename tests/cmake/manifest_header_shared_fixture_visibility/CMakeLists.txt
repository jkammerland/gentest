cmake_minimum_required(VERSION 3.31)
project(gentest_manifest_header_shared_fixture_visibility LANGUAGES CXX)

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

add_executable(mhsf_tests
    cases.cpp)

target_link_libraries(mhsf_tests PRIVATE gentest gentest_main)
target_compile_features(mhsf_tests PRIVATE cxx_std_20)
target_include_directories(mhsf_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

gentest_attach_codegen(mhsf_tests
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/mhsf_tests.cpp")
