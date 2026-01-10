cmake_minimum_required(VERSION 3.31)
project(gentest_codegen_output_collision LANGUAGES CXX)

# Include the in-tree helper under test.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/GentestCodegen.cmake")

# Satisfy gentest_attach_codegen's host tool selection during configure.
# We won't build; we only want to validate configure-time collision checks.
set(GENTEST_CODEGEN_EXECUTABLE "${CMAKE_COMMAND}")

# Generate minimal inputs in the build dir (keeps this fixture source-only).
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/main.cpp" "int main() { return 0; }\n")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/case1.cpp" "void case1() {}\n")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/case2.cpp" "void case2() {}\n")

add_executable(t1 "${CMAKE_CURRENT_BINARY_DIR}/main.cpp")
add_executable(t2 "${CMAKE_CURRENT_BINARY_DIR}/main.cpp")

# Intentional collision: both targets write the same OUTPUT.
gentest_attach_codegen(t1 OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/out/test_impl.cpp" SOURCES "${CMAKE_CURRENT_BINARY_DIR}/case1.cpp")
gentest_attach_codegen(t2 OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/out/test_impl.cpp" SOURCES "${CMAKE_CURRENT_BINARY_DIR}/case2.cpp")

