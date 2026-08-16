cmake_minimum_required(VERSION 3.31)
project(gentest_hello_header_only LANGUAGES CXX)

include(CTest)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(gentest CONFIG REQUIRED)

add_executable(gentest_hello_header_only cases.hpp)
target_compile_features(gentest_hello_header_only PRIVATE cxx_std_20)
target_link_libraries(gentest_hello_header_only PRIVATE gentest::gentest_main)

gentest_attach_codegen(gentest_hello_header_only
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/gentest_codegen")
gentest_discover_tests(gentest_hello_header_only)
