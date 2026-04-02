cmake_minimum_required(VERSION 3.31)
project(gentest_mock_template_template_ctor_traits LANGUAGES CXX)

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

gentest_add_mocks(template_template_ctor_traits_mocks
    DEFS
        ${CMAKE_CURRENT_SOURCE_DIR}/mock_defs.hpp
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/mocks"
    HEADER_NAME public/template_template_ctor_traits_mocks.hpp)

add_executable(mock_template_template_ctor_traits traits.cpp)
target_compile_features(mock_template_template_ctor_traits PRIVATE cxx_std_20)
target_include_directories(mock_template_template_ctor_traits PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
gentest_link_mocks(mock_template_template_ctor_traits PRIVATE template_template_ctor_traits_mocks)
