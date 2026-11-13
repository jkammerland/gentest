cmake_minimum_required(VERSION 3.31)
project(gentest_explicit_mock_target_surface LANGUAGES CXX)

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

include("${GENTEST_SOURCE_DIR}/tests/cmake/GentestFixtureDependency.cmake")
gentest_fixture_add_gentest(gentest)

gentest_add_mocks(explicit_header_mocks
    DEFS
        ${CMAKE_CURRENT_SOURCE_DIR}/header_mocks.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/aggregate_header_mocks.hpp
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/header"
    HEADER_NAME public/fixture_header_mocks.hpp)

gentest_add_mocks(explicit_header_mocks_extra
    DEFS
        ${CMAKE_CURRENT_SOURCE_DIR}/alt_header_mocks.hpp
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/header_extra"
    HEADER_NAME public/fixture_alt_header_mocks.hpp)

gentest_add_mocks(explicit_module_mocks
    DEFS
        ${CMAKE_CURRENT_SOURCE_DIR}/service_module.cppm
        ${CMAKE_CURRENT_SOURCE_DIR}/module_mocks.cppm
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/module"
    MODULE_NAME fixture.explicit_module_mocks)

add_executable(explicit_header_consumer header_consumer.cpp)
target_compile_features(explicit_header_consumer PRIVATE cxx_std_20)
gentest_link_mocks(explicit_header_consumer PRIVATE explicit_header_mocks explicit_header_mocks_extra)

add_executable(explicit_module_consumer module_consumer.cpp)
target_compile_features(explicit_module_consumer PRIVATE cxx_std_20)
gentest_link_mocks(explicit_module_consumer PRIVATE explicit_module_mocks)

add_executable(explicit_late_link_consumer late_link_cases.cpp)
target_compile_features(explicit_late_link_consumer PRIVATE cxx_std_20)
target_link_libraries(explicit_late_link_consumer PRIVATE gentest::gentest_main)
gentest_attach_codegen(explicit_late_link_consumer
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/late_link")
gentest_link_mocks(explicit_late_link_consumer PRIVATE explicit_header_mocks)

# Only the compile command changes when this probe is enabled; the sources stay put.
if(GENTEST_EXPLICIT_MOCK_COMMAND_PROBE)
    target_compile_definitions(explicit_late_link_consumer PRIVATE GENTEST_EXPLICIT_MOCK_COMMAND_PROBE=73)
endif()

add_executable(explicit_late_link_module_consumer
    late_link_module_main.cpp)
target_sources(explicit_late_link_module_consumer
    PRIVATE
        FILE_SET explicit_late_link_module_cases TYPE CXX_MODULES FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/late_link_module_cases.cppm)
target_link_libraries(explicit_late_link_module_consumer PRIVATE gentest::gentest gentest::gentest_runtime)
target_compile_features(explicit_late_link_module_consumer PRIVATE cxx_std_20)
gentest_attach_codegen(explicit_late_link_module_consumer
    MODULE_REGISTRATION
    FILE_SET explicit_late_link_module_cases
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/late_link_module")
gentest_link_mocks(explicit_late_link_module_consumer PRIVATE explicit_module_mocks)

# #239: DEFS and the production headers they mock are used in place, so a
# consumer can include both, in either order, with #pragma once alone. One
# mocked type sits beside the DEFS; the other comes from a linked library.
add_library(pragma_once_account_api INTERFACE)
target_include_directories(pragma_once_account_api INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/pragma_once/api")

gentest_add_mocks(pragma_once_mocks
    DEFS
        ${CMAKE_CURRENT_SOURCE_DIR}/pragma_once/ledger_mocks.hpp
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/pragma_once"
    HEADER_NAME public/pragma_once_mocks.hpp
    LINK_LIBRARIES pragma_once_account_api)

foreach(_pragma_once_order IN ITEMS production_first mock_first)
    add_executable(pragma_once_${_pragma_once_order} pragma_once/${_pragma_once_order}.cpp)
    target_compile_features(pragma_once_${_pragma_once_order} PRIVATE cxx_std_20)
    target_link_libraries(pragma_once_${_pragma_once_order} PRIVATE gentest::gentest_main)
    gentest_link_mocks(pragma_once_${_pragma_once_order} PRIVATE pragma_once_mocks)
    gentest_attach_codegen(pragma_once_${_pragma_once_order}
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/pragma_once_${_pragma_once_order}")
endforeach()
