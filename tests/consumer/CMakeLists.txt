cmake_minimum_required(VERSION 3.31)
project(gentest_consumer LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(gentest CONFIG REQUIRED)

add_executable(gentest_consumer)

target_compile_features(gentest_consumer PRIVATE cxx_std_20)
target_sources(gentest_consumer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/assertions_header_isolation.cpp)

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
    gentest_add_mocks(gentest_consumer_explicit_mocks
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/service_module.cppm
            ${CMAKE_CURRENT_SOURCE_DIR}/module_mock_defs.cppm
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/mocks"
        MODULE_NAME gentest.consumer_mocks)
    target_sources(gentest_consumer
        PRIVATE
            FILE_SET module_cases TYPE CXX_MODULES FILES
                ${CMAKE_CURRENT_SOURCE_DIR}/cases.cppm)
    gentest_link_mocks(gentest_consumer PRIVATE gentest_consumer_explicit_mocks)
else()
    gentest_add_mocks(gentest_consumer_explicit_mocks
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/header_mock_defs.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/mocks"
        HEADER_NAME gentest_consumer_mocks.hpp)
    target_sources(gentest_consumer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/cases.cpp)
    gentest_link_mocks(gentest_consumer PRIVATE gentest_consumer_explicit_mocks)
endif()

gentest_attach_codegen(gentest_consumer
    OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/gentest_codegen)
