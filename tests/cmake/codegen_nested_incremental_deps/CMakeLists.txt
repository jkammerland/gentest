cmake_minimum_required(VERSION 3.31)
project(gentest_codegen_nested_incremental_deps LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
include("${GENTEST_SOURCE_DIR}/cmake/GentestCodegen.cmake")

# The compilation database belongs to the root; the nested directory consumes DEPFILE.
add_subdirectory(nested)
