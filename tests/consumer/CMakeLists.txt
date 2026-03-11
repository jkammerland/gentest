cmake_minimum_required(VERSION 3.31)
project(gentest_consumer LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(gentest CONFIG REQUIRED)

add_executable(gentest_consumer
    main.cpp)

target_compile_features(gentest_consumer PRIVATE cxx_std_20)
target_link_libraries(gentest_consumer PRIVATE gentest::gentest_main gentest::gentest_runtime)

target_sources(gentest_consumer
    PRIVATE
        FILE_SET module_cases TYPE CXX_MODULES FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/cases.cppm)

gentest_attach_codegen(gentest_consumer
    OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/gentest_codegen)
