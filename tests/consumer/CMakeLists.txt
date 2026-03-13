cmake_minimum_required(VERSION 3.31)
project(gentest_consumer LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(gentest CONFIG REQUIRED)

add_executable(gentest_consumer)

target_compile_features(gentest_consumer PRIVATE cxx_std_20)

set(GENTEST_CONSUMER_LINK_MODE "double" CACHE STRING
    "Link mode for the consumer fixture: main_only, runtime_only, or double")
set_property(CACHE GENTEST_CONSUMER_LINK_MODE PROPERTY STRINGS main_only runtime_only double)
option(GENTEST_CONSUMER_USE_MODULES "Build the consumer fixture through public named modules" ON)

if(GENTEST_CONSUMER_LINK_MODE STREQUAL "main_only")
    if(GENTEST_CONSUMER_USE_MODULES)
        target_link_libraries(gentest_consumer PRIVATE gentest::gentest gentest::gentest_main)
    else()
        target_link_libraries(gentest_consumer PRIVATE gentest::gentest_main)
    endif()
elseif(GENTEST_CONSUMER_LINK_MODE STREQUAL "runtime_only")
    target_sources(gentest_consumer PRIVATE main.cpp)
    if(GENTEST_CONSUMER_USE_MODULES)
        target_link_libraries(gentest_consumer PRIVATE gentest::gentest gentest::gentest_runtime)
    else()
        target_link_libraries(gentest_consumer PRIVATE gentest::gentest_runtime)
    endif()
elseif(GENTEST_CONSUMER_LINK_MODE STREQUAL "double")
    target_sources(gentest_consumer PRIVATE main.cpp)
    if(GENTEST_CONSUMER_USE_MODULES)
        target_link_libraries(gentest_consumer PRIVATE gentest::gentest gentest::gentest_main gentest::gentest_runtime)
    else()
        target_link_libraries(gentest_consumer PRIVATE gentest::gentest_main gentest::gentest_runtime)
    endif()
else()
    message(FATAL_ERROR
        "Unsupported GENTEST_CONSUMER_LINK_MODE='${GENTEST_CONSUMER_LINK_MODE}' "
        "(expected main_only, runtime_only, or double)")
endif()

if(GENTEST_CONSUMER_USE_MODULES)
    target_compile_definitions(gentest_consumer PRIVATE GENTEST_CONSUMER_USE_MODULES=1)
    target_sources(gentest_consumer
        PRIVATE
            FILE_SET module_cases TYPE CXX_MODULES FILES
                ${CMAKE_CURRENT_SOURCE_DIR}/cases.cppm)
else()
    target_sources(gentest_consumer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/cases.cpp)
endif()

gentest_attach_codegen(gentest_consumer
    OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/gentest_codegen)
