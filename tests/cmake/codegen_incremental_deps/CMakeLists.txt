cmake_minimum_required(VERSION 3.31)
project(gentest_codegen_incremental_deps LANGUAGES CXX)

if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
    message(FATAL_ERROR "GENTEST_SOURCE_DIR must point at the gentest source tree")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(DEP_SWITCH "0" CACHE STRING "Compile-definition toggle for incremental codegen dependency coverage")
set(gentest_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(GENTEST_BUILD_CODEGEN ON CACHE BOOL "" FORCE)

add_subdirectory("${GENTEST_SOURCE_DIR}" gentest)

add_executable(dep_tests cases.cpp)
target_link_libraries(dep_tests PRIVATE gentest_main)
target_compile_features(dep_tests PRIVATE cxx_std_20)
target_include_directories(dep_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(dep_tests PRIVATE DEP_SWITCH=${DEP_SWITCH})

gentest_attach_codegen(dep_tests
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
    CLANG_ARGS
        -I${CMAKE_CURRENT_SOURCE_DIR})
