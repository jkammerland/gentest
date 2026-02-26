cmake_minimum_required(VERSION 3.31)
project(gentest_manifest_named_module_reject LANGUAGES CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/GentestCodegen.cmake")

# Configure-time fixture only.
set(GENTEST_CODEGEN_EXECUTABLE "${CMAKE_COMMAND}")

option(USE_NO_INCLUDE_SOURCES "Use NO_INCLUDE_SOURCES for manifest mode" OFF)

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/main.cpp" "int main() { return 0; }\n")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/module_case.cpp"
    "export module fixture.case;\n"
    "export int value() { return 1; }\n")

add_executable(demo
    "${CMAKE_CURRENT_BINARY_DIR}/main.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/module_case.cpp")

if(USE_NO_INCLUDE_SOURCES)
    gentest_attach_codegen(demo
        SOURCES "${CMAKE_CURRENT_BINARY_DIR}/module_case.cpp"
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated.cpp"
        NO_INCLUDE_SOURCES)
else()
    gentest_attach_codegen(demo
        SOURCES "${CMAKE_CURRENT_BINARY_DIR}/module_case.cpp"
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated.cpp")
endif()
