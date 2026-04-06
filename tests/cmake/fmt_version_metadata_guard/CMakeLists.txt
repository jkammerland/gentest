cmake_minimum_required(VERSION 3.31)
project(gentest_fmt_version_metadata_guard LANGUAGES CXX)

if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
    message(FATAL_ERROR "GENTEST_SOURCE_DIR must point at the gentest source tree")
endif()

set(CMAKE_CXX_EXTENSIONS OFF)
set(gentest_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(GENTEST_BUILD_CODEGEN OFF CACHE BOOL "" FORCE)
set(GENTEST_ENABLE_PUBLIC_MODULES OFF CACHE STRING "" FORCE)

include(FetchContent)

# Provide a buildable predefined fmt target without version metadata. The
# guard is about gentest's install/export probe, not about whether the host
# environment happens to expose <fmt/...> through ambient compiler paths.
FetchContent_Populate(
    fmt_headers
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 12.1.0)

add_library(fmt_predefined INTERFACE)
target_include_directories(fmt_predefined INTERFACE "${fmt_headers_SOURCE_DIR}/include")
target_compile_definitions(fmt_predefined INTERFACE FMT_HEADER_ONLY=1)
add_library(fmt::fmt ALIAS fmt_predefined)

add_subdirectory("${GENTEST_SOURCE_DIR}" gentest)
