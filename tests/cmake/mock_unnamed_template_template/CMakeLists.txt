cmake_minimum_required(VERSION 3.31)
project(gentest_mock_unnamed_template_template LANGUAGES CXX)

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

gentest_add_mocks(mock_unnamed_template_template_mocks
    DEFS
        ${CMAKE_CURRENT_SOURCE_DIR}/mock_defs.hpp
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/mocks"
    HEADER_NAME public/mock_unnamed_template_template_mocks.hpp)

add_executable(mock_unnamed_template_template cases.cpp)
target_compile_features(mock_unnamed_template_template PRIVATE cxx_std_20)
target_include_directories(mock_unnamed_template_template PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mock_unnamed_template_template PRIVATE gentest_main)
gentest_link_mocks(mock_unnamed_template_template PRIVATE mock_unnamed_template_template_mocks)

gentest_attach_codegen(mock_unnamed_template_template
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
