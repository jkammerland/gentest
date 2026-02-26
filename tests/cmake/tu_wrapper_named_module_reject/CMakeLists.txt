cmake_minimum_required(VERSION 3.31)
project(gentest_tu_wrapper_named_module_reject LANGUAGES CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/GentestCodegen.cmake")

# This fixture validates configure-time diagnostics only.
set(GENTEST_CODEGEN_EXECUTABLE "${CMAKE_COMMAND}")

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/main.cpp" "int main() { return 0; }\n")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/module_case.cpp"
    "export module fixture.case;\n"
    "export int value() { return 1; }\n")

add_executable(demo
    "${CMAKE_CURRENT_BINARY_DIR}/main.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/module_case.cpp")

gentest_attach_codegen(demo SOURCES "${CMAKE_CURRENT_BINARY_DIR}/module_case.cpp")
