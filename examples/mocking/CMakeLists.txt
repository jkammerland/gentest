cmake_minimum_required(VERSION 3.31)
project(gentest_mocking LANGUAGES CXX)

include(CTest)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(gentest CONFIG REQUIRED)

gentest_add_mocks(payment_mocks
    DEFS "${CMAKE_CURRENT_SOURCE_DIR}/payment_mocks.hpp"
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/mocks"
    HEADER_NAME public/payment_mocks.hpp)

add_executable(gentest_mocking cases.hpp)
target_compile_features(gentest_mocking PRIVATE cxx_std_20)
target_link_libraries(gentest_mocking PRIVATE gentest::gentest_main)
# Link the generated mock surface before attaching test codegen.
gentest_link_mocks(gentest_mocking PRIVATE payment_mocks)

gentest_attach_codegen(gentest_mocking
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/gentest_codegen")
gentest_discover_tests(gentest_mocking)
