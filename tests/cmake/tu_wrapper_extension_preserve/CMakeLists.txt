cmake_minimum_required(VERSION 3.31)
project(gentest_tu_wrapper_extension_preserve LANGUAGES CXX)

# Include the in-tree helper under test.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/GentestCodegen.cmake")

# Satisfy gentest_attach_codegen's host tool selection during configure.
# This fixture validates configure-time source replacement + properties only.
set(GENTEST_CODEGEN_EXECUTABLE "${CMAKE_COMMAND}")

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/main.cpp" "int main() { return 0; }\n")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/case.cu" "void dummy() {}\n")

add_executable(demo "${CMAKE_CURRENT_BINARY_DIR}/main.cpp" "${CMAKE_CURRENT_BINARY_DIR}/case.cu")

set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/case.cu" PROPERTIES
    LANGUAGE CXX
    COMPILE_DEFINITIONS "DEMO_CU_PROP=1"
    COMPILE_DEFINITIONS_DEBUG "DEMO_CU_DEBUG=1"
    COMPILE_OPTIONS_DEBUG "-Wno-unused-parameter"
    CUDA_ARCHITECTURES "75")

gentest_attach_codegen(demo SOURCES "${CMAKE_CURRENT_BINARY_DIR}/case.cu")

get_target_property(_sources demo SOURCES)
set(_wrapper "")
foreach(_src IN LISTS _sources)
    if(_src MATCHES "\\.gentest\\.cu$")
        set(_wrapper "${_src}")
        break()
    endif()
endforeach()

if(_wrapper STREQUAL "")
    message(FATAL_ERROR "Expected a .gentest.cu wrapper source in target SOURCES, got '${_sources}'")
endif()

get_source_file_property(_language "${_wrapper}" LANGUAGE)
if(NOT _language STREQUAL "CXX")
    message(FATAL_ERROR "Wrapper LANGUAGE mismatch. Expected 'CXX', got '${_language}'")
endif()

get_source_file_property(_defs "${_wrapper}" COMPILE_DEFINITIONS)
if(NOT _defs STREQUAL "DEMO_CU_PROP=1")
    message(FATAL_ERROR "Wrapper COMPILE_DEFINITIONS mismatch. Expected 'DEMO_CU_PROP=1', got '${_defs}'")
endif()

get_source_file_property(_defs_debug "${_wrapper}" COMPILE_DEFINITIONS_DEBUG)
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT _defs_debug STREQUAL "DEMO_CU_DEBUG=1")
    message(FATAL_ERROR "Wrapper COMPILE_DEFINITIONS_DEBUG mismatch. Expected 'DEMO_CU_DEBUG=1', got '${_defs_debug}'")
endif()

get_source_file_property(_opts_debug "${_wrapper}" COMPILE_OPTIONS_DEBUG)
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT _opts_debug STREQUAL "-Wno-unused-parameter")
    message(FATAL_ERROR "Wrapper COMPILE_OPTIONS_DEBUG mismatch. Expected '-Wno-unused-parameter', got '${_opts_debug}'")
endif()

get_source_file_property(_cuda_arch "${_wrapper}" CUDA_ARCHITECTURES)
if(NOT _cuda_arch STREQUAL "75")
    message(FATAL_ERROR "Wrapper CUDA_ARCHITECTURES mismatch. Expected '75', got '${_cuda_arch}'")
endif()
