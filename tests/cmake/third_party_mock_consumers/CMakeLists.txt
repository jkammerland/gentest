cmake_minimum_required(VERSION 3.31)
project(gentest_third_party_mock_consumers LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GENTEST_THIRD_PARTY_MOCK_CONSUMERS_USE_PACKAGE "Consume gentest through find_package instead of add_subdirectory" OFF)
if(GENTEST_THIRD_PARTY_MOCK_CONSUMERS_USE_PACKAGE)
    find_package(gentest CONFIG REQUIRED)
else()
    if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
        message(FATAL_ERROR "GENTEST_SOURCE_DIR must point at the gentest source tree")
    endif()

    set(gentest_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    if(DEFINED GENTEST_CODEGEN_EXECUTABLE AND NOT "${GENTEST_CODEGEN_EXECUTABLE}" STREQUAL "")
        set(GENTEST_BUILD_CODEGEN OFF CACHE BOOL "" FORCE)
    else()
        set(GENTEST_BUILD_CODEGEN ON CACHE BOOL "" FORCE)
    endif()

    add_subdirectory("${GENTEST_SOURCE_DIR}" gentest)
endif()

option(GENTEST_REQUIRE_REAL_THIRD_PARTY_MOCK_CONSUMERS "Require real GMock and doctest generated-mock consumers" OFF)
option(GENTEST_REQUIRE_REAL_TROMPELOEIL_MOCK_CONSUMER "Require real Trompeloeil generated-mock consumer" OFF)

find_package(GTest CONFIG QUIET)
if(NOT GTest_FOUND OR NOT TARGET GTest::gmock OR NOT TARGET GTest::gtest_main)
    find_package(GTest QUIET)
endif()
find_package(doctest CONFIG QUIET)
if(NOT doctest_FOUND OR NOT TARGET doctest::doctest)
    find_package(doctest QUIET)
endif()
find_package(trompeloeil CONFIG QUIET)
if(NOT trompeloeil_FOUND OR NOT TARGET trompeloeil::trompeloeil)
    find_package(trompeloeil QUIET)
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
    message(STATUS "GTest/GMock targets GTest::gmock and GTest::gtest_main not found; real generated-mock consumers will be skipped")
endif()

if(trompeloeil_FOUND AND TARGET trompeloeil::trompeloeil)
    gentest_add_mocks(real_trompeloeil_mocks
        DEFS
            ${CMAKE_CURRENT_SOURCE_DIR}/gmock_defs.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/trompeloeil"
        HEADER_NAME public/real_trompeloeil_mocks.hpp
        BACKEND trompeloeil
        LINK_LIBRARIES trompeloeil::trompeloeil)

    add_executable(real_trompeloeil_mock_consumer trompeloeil_consumer.cpp)
    target_compile_features(real_trompeloeil_mock_consumer PRIVATE cxx_std_20)
    target_link_libraries(real_trompeloeil_mock_consumer PRIVATE real_trompeloeil_mocks trompeloeil::trompeloeil)
    list(APPEND _gentest_real_consumer_targets real_trompeloeil_mock_consumer)
    file(WRITE "${_gentest_real_consumer_stamp_dir}/trompeloeil.txt" "real_trompeloeil_mock_consumer\n")
else()
    message(STATUS "Trompeloeil target trompeloeil::trompeloeil not found; real generated-mock consumer will be skipped")
endif()

if(GENTEST_REQUIRE_REAL_THIRD_PARTY_MOCK_CONSUMERS)
    if(NOT EXISTS "${_gentest_real_consumer_stamp_dir}/gtest.txt")
        message(FATAL_ERROR "GENTEST_REQUIRE_REAL_THIRD_PARTY_MOCK_CONSUMERS=ON requires GTest::gmock and GTest::gtest_main")
    endif()
    if(NOT EXISTS "${_gentest_real_consumer_stamp_dir}/doctest.txt")
        message(FATAL_ERROR "GENTEST_REQUIRE_REAL_THIRD_PARTY_MOCK_CONSUMERS=ON requires doctest::doctest")
    endif()
endif()
if(GENTEST_REQUIRE_REAL_TROMPELOEIL_MOCK_CONSUMER AND NOT EXISTS "${_gentest_real_consumer_stamp_dir}/trompeloeil.txt")
    message(FATAL_ERROR "GENTEST_REQUIRE_REAL_TROMPELOEIL_MOCK_CONSUMER=ON requires trompeloeil::trompeloeil")
endif()

if(_gentest_real_consumer_targets)
    add_custom_target(real_mock_consumers ALL DEPENDS ${_gentest_real_consumer_targets})
endif()
