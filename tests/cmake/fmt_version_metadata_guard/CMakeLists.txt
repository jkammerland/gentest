cmake_minimum_required(VERSION 3.31)
project(gentest_fmt_version_metadata_guard LANGUAGES CXX)

if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
    message(FATAL_ERROR "GENTEST_SOURCE_DIR must point at the gentest source tree")
endif()

set(CMAKE_CXX_EXTENSIONS OFF)
set(gentest_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(GENTEST_BUILD_CODEGEN OFF CACHE BOOL "" FORCE)
set(GENTEST_ENABLE_PUBLIC_MODULES OFF CACHE STRING "" FORCE)

add_library(fmt_predefined INTERFACE)
add_library(fmt::fmt ALIAS fmt_predefined)

add_subdirectory("${GENTEST_SOURCE_DIR}" gentest)
