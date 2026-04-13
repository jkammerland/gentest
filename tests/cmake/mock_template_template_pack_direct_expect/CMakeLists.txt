cmake_minimum_required(VERSION 3.31)
project(gentest_mock_template_template_pack_direct_expect LANGUAGES CXX)

if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
    message(FATAL_ERROR "GENTEST_SOURCE_DIR must point at the gentest source tree")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(WIN32)
    set(CMAKE_OBJECT_PATH_MAX 128)
endif()
set(gentest_BUILD_TESTING OFF CACHE BOOL "" FORCE)
if(DEFINED GENTEST_CODEGEN_EXECUTABLE AND NOT "${GENTEST_CODEGEN_EXECUTABLE}" STREQUAL "")
    set(GENTEST_BUILD_CODEGEN OFF CACHE BOOL "" FORCE)
else()
    set(GENTEST_BUILD_CODEGEN ON CACHE BOOL "" FORCE)
endif()
set(_gentest_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/g")
set(_gentest_mock_dir "${_gentest_generated_dir}/m")

add_subdirectory("${GENTEST_SOURCE_DIR}" gentest)

gentest_add_mocks(template_template_pack_direct_expect_mocks
    DEFS
        ${CMAKE_CURRENT_SOURCE_DIR}/mock_defs.hpp
    OUTPUT_DIR "${_gentest_mock_dir}"
    HEADER_NAME public/template_template_pack_direct_expect_mocks.hpp)

add_executable(mock_template_template_pack_direct_expect main.cpp)
target_compile_features(mock_template_template_pack_direct_expect PRIVATE cxx_std_20)
target_include_directories(mock_template_template_pack_direct_expect PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(mock_template_template_pack_direct_expect PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")
gentest_link_mocks(mock_template_template_pack_direct_expect PRIVATE template_template_pack_direct_expect_mocks)
