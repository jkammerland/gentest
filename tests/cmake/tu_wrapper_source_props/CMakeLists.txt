cmake_minimum_required(VERSION 3.31)
project(gentest_textual_wrapper_compatibility_removed LANGUAGES CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/GentestCodegen.cmake")

set(GENTEST_CODEGEN_EXECUTABLE "${CMAKE_COMMAND}")

add_executable(demo main.cpp)
set_property(TARGET demo PROPERTY GENTEST_INTERNAL_TEXTUAL_WRAPPER_COMPATIBILITY ON)
gentest_attach_codegen(demo)
