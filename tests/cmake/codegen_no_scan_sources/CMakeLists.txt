cmake_minimum_required(VERSION 3.31)
project(gentest_codegen_no_scan_sources LANGUAGES CXX)

# Include the in-tree helper under test.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/GentestCodegen.cmake")

# Satisfy gentest_attach_codegen's host tool selection during configure.
# We won't build; we only want to validate the configure-time guard.
set(GENTEST_CODEGEN_EXECUTABLE "${CMAKE_COMMAND}")

# The target has a source, but it is neither a C++ translation unit nor an
# annotated header, so header-declaration registration must reject it at
# configure time instead of generating an empty registration target.
add_executable(no_scan_sources_test "${CMAKE_CURRENT_LIST_DIR}/notes.txt")
gentest_attach_codegen(no_scan_sources_test OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
