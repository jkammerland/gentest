# Factory registration demo
# Demonstrates the new factory-based registration model

cmake_minimum_required(VERSION 3.31)
project(factory_demo LANGUAGES C CXX)

# Find gentest (assumes it's installed or in CMAKE_PREFIX_PATH)
find_package(gentest CONFIG REQUIRED)

add_executable(factory_demo
    cases.cpp
)

target_compile_features(factory_demo PRIVATE cxx_std_20)
target_link_libraries(factory_demo PRIVATE gentest::gentest_main)

# Use factory registration instead of traditional TU wrapper mode
gentest_attach_factory_codegen(factory_demo)
