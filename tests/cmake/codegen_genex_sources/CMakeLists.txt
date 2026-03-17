cmake_minimum_required(VERSION 3.31)
project(gentest_codegen_genex_sources LANGUAGES CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/GentestCodegen.cmake")

set(GENTEST_CODEGEN_EXECUTABLE "${CMAKE_COMMAND}")

add_executable(genex_sources
    "${CMAKE_CURRENT_LIST_DIR}/main.cpp"
    "$<$<BOOL:1>:${CMAKE_CURRENT_LIST_DIR}/cases.cpp>")

gentest_attach_codegen(genex_sources)
