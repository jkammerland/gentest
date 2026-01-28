cmake_minimum_required(VERSION 3.31)
project(gentest_fuzztest_optional LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../../cmake")
include(GentestCodegen)

add_library(gentest_fuzztest_optional STATIC dummy.cpp)
target_include_directories(gentest_fuzztest_optional PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../../include")

gentest_attach_fuzztest_codegen(gentest_fuzztest_optional
    SOURCES ${CMAKE_CURRENT_LIST_DIR}/dummy.cpp
    NO_INCLUDE_SOURCES)
