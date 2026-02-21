cmake_minimum_required(VERSION 3.31)
project(gentest_tu_header_case_collision LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

get_filename_component(_repo_root "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
include("${_repo_root}/cmake/GentestCodegen.cmake")

if(NOT DEFINED CODEGEN_EXECUTABLE OR CODEGEN_EXECUTABLE STREQUAL "")
  message(FATAL_ERROR "CODEGEN_EXECUTABLE must be provided")
endif()
set(GENTEST_CODEGEN_EXECUTABLE "${CODEGEN_EXECUTABLE}")

add_library(case_collision_obj OBJECT
  lower_case.cpp
  Lower_Case.cpp)

target_compile_features(case_collision_obj PRIVATE cxx_std_20)
target_include_directories(case_collision_obj PRIVATE "${_repo_root}/include")

gentest_attach_codegen(case_collision_obj
  OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
  CLANG_ARGS
    -std=c++20
    -I${_repo_root}/include
    -I${CMAKE_CURRENT_LIST_DIR})
