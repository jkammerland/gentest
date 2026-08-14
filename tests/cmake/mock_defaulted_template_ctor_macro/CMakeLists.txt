cmake_minimum_required(VERSION 3.31)
project(gentest_mock_defaulted_template_ctor_macro LANGUAGES CXX)

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

gentest_add_mocks(defaulted_template_ctor_macro_mocks
    DEFS
        ${CMAKE_CURRENT_SOURCE_DIR}/mock_defs.hpp
    OUTPUT_DIR "${_gentest_mock_dir}"
    HEADER_NAME public/defaulted_template_ctor_macro_mocks.hpp)

add_executable(mock_defaulted_template_ctor_macro cases.cpp)
target_compile_features(mock_defaulted_template_ctor_macro PRIVATE cxx_std_20)
target_include_directories(mock_defaulted_template_ctor_macro PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mock_defaulted_template_ctor_macro PRIVATE gentest_main)
gentest_link_mocks(mock_defaulted_template_ctor_macro PRIVATE defaulted_template_ctor_macro_mocks)

gentest_attach_codegen(mock_defaulted_template_ctor_macro
    OUTPUT_DIR "${_gentest_generated_dir}")
