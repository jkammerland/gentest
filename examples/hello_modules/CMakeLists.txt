cmake_minimum_required(VERSION 3.31)
project(gentest_hello_modules LANGUAGES CXX)

include(CTest)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(gentest CONFIG REQUIRED)

get_target_property(_gentest_module_sets gentest::gentest INTERFACE_CXX_MODULE_SETS)
if(NOT _gentest_module_sets)
    get_target_property(_gentest_module_sets gentest::gentest CXX_MODULE_SETS)
endif()
if(NOT _gentest_module_sets)
    message(FATAL_ERROR
        "examples/hello_modules requires gentest to be installed with -DGENTEST_ENABLE_PUBLIC_MODULES=ON")
endif()

add_executable(gentest_hello_modules)
target_compile_features(gentest_hello_modules PRIVATE cxx_std_20)
target_sources(gentest_hello_modules
    PRIVATE
        FILE_SET module_cases TYPE CXX_MODULES FILES
            cases.cppm)
target_link_libraries(gentest_hello_modules PRIVATE
    gentest::gentest
    gentest::gentest_main)

gentest_attach_codegen(gentest_hello_modules
    MODULE_REGISTRATION
    FILE_SET module_cases
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/gentest_codegen")
gentest_discover_tests(gentest_hello_modules)
