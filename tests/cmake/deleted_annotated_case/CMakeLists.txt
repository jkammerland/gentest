cmake_minimum_required(VERSION 3.31)
project(gentest_deleted_annotated_case LANGUAGES CXX)

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

add_executable(deleted_annotated_case cases.cpp)
target_compile_features(deleted_annotated_case PRIVATE cxx_std_20)
target_link_libraries(deleted_annotated_case PRIVATE gentest::gentest_main)
gentest_attach_codegen(deleted_annotated_case OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
