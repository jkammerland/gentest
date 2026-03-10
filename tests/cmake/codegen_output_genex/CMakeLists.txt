cmake_minimum_required(VERSION 3.31)
project(gentest_codegen_output_genex LANGUAGES CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/GentestCodegen.cmake")

set(GENTEST_CODEGEN_EXECUTABLE "${CMAKE_COMMAND}")

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/main.cpp" "int main() { return 0; }\n")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/case.cpp" "[[maybe_unused]] void manifest_case() {}\n")

add_executable(genex_output "${CMAKE_CURRENT_BINARY_DIR}/main.cpp")

gentest_attach_codegen(
    genex_output
    OUTPUT "$<$<BOOL:1>:${CMAKE_CURRENT_BINARY_DIR}/out/generated.cpp>"
    SOURCES "${CMAKE_CURRENT_BINARY_DIR}/case.cpp")
