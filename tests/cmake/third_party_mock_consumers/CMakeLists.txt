cmake_minimum_required(VERSION 3.31)
project(gentest_third_party_mock_consumers LANGUAGES CXX)

if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
    message(FATAL_ERROR "GENTEST_SOURCE_DIR must point at the gentest source tree")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(gentest_BUILD_TESTING OFF CACHE BOOL "" FORCE)
if(DEFINED GENTEST_CODEGEN_EXECUTABLE AND NOT "${GENTEST_CODEGEN_EXECUTABLE}" STREQUAL "")
    set(GENTEST_BUILD_CODEGEN OFF CACHE BOOL "" FORCE)
else()
    set(GENTEST_BUILD_CODEGEN ON CACHE BOOL "" FORCE)
endif()

add_subdirectory("${GENTEST_SOURCE_DIR}" gentest)

find_package(GTest CONFIG QUIET)
if(NOT GTest_FOUND)
    find_package(GTest QUIET)
endif()
find_package(doctest CONFIG QUIET)
if(NOT doctest_FOUND)
    find_package(doctest QUIET)
endif()

set(_gentest_real_consumer_targets "")
set(_gentest_real_consumer_stamp_dir "${CMAKE_CURRENT_BINARY_DIR}/consumer-stamps")
file(MAKE_DIRECTORY "${_gentest_real_consumer_stamp_dir}")

if(GTest_FOUND AND TARGET GTest::gmock AND TARGET GTest::gtest_main)
    gentest_add_mocks(real_gmock_mocks
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/gmock_defs.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/gmock"
        HEADER_NAME public/real_gmock_mocks.hpp
        BACKEND gmock
        LINK_LIBRARIES GTest::gmock)

    add_executable(real_gtest_mock_consumer gtest_consumer.cpp)
    target_compile_features(real_gtest_mock_consumer PRIVATE cxx_std_20)
    target_link_libraries(real_gtest_mock_consumer PRIVATE real_gmock_mocks GTest::gtest_main)
    list(APPEND _gentest_real_consumer_targets real_gtest_mock_consumer)
    file(WRITE "${_gentest_real_consumer_stamp_dir}/gtest.txt" "real_gtest_mock_consumer\n")

    if(doctest_FOUND AND TARGET doctest::doctest)
        add_executable(real_doctest_mock_consumer doctest_consumer.cpp)
        target_compile_features(real_doctest_mock_consumer PRIVATE cxx_std_20)
        target_link_libraries(real_doctest_mock_consumer PRIVATE real_gmock_mocks GTest::gmock doctest::doctest)
        list(APPEND _gentest_real_consumer_targets real_doctest_mock_consumer)
        file(WRITE "${_gentest_real_consumer_stamp_dir}/doctest.txt" "real_doctest_mock_consumer\n")
    else()
        message(STATUS "doctest package not found; doctest generated-mock consumer will be skipped")
    endif()
else()
    message(STATUS "GTest/GMock package not found; real generated-mock consumers will be skipped")
endif()

if(_gentest_real_consumer_targets)
    add_custom_target(real_mock_consumers ALL DEPENDS ${_gentest_real_consumer_targets})
endif()
