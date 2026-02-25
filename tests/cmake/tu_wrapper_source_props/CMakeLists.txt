cmake_minimum_required(VERSION 3.31)
project(gentest_tu_wrapper_source_props LANGUAGES CXX)

# Include the in-tree helper under test.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/GentestCodegen.cmake")

# Satisfy gentest_attach_codegen's host tool selection during configure.
# This fixture validates configure-time source replacement + properties only.
set(GENTEST_CODEGEN_EXECUTABLE "${CMAKE_COMMAND}")

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/main.cpp" "int main() { return 0; }\n")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/case.cpp" "void dummy() {}\n")

add_executable(demo "${CMAKE_CURRENT_BINARY_DIR}/main.cpp" "${CMAKE_CURRENT_BINARY_DIR}/case.cpp")

set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/case.cpp" PROPERTIES
    COMPILE_DEFINITIONS "DEMO_PROP=1"
    COMPILE_OPTIONS "-Wno-unused-parameter"
    INCLUDE_DIRECTORIES "${CMAKE_CURRENT_BINARY_DIR}"
    COMPILE_FLAGS "-Wno-unused-variable"
)

set_property(TARGET demo PROPERTY UNITY_BUILD ON)

gentest_attach_codegen(demo SOURCES "${CMAKE_CURRENT_BINARY_DIR}/case.cpp")

get_target_property(_sources demo SOURCES)
set(_wrapper "")
foreach(_src IN LISTS _sources)
    if(_src MATCHES "\\.gentest\\.[A-Za-z0-9+]+$")
        if(_wrapper STREQUAL "")
            set(_wrapper "${_src}")
        else()
            message(FATAL_ERROR "Expected a single wrapper source, found multiple: '${_wrapper}' and '${_src}'")
        endif()
    endif()
endforeach()

if(_wrapper STREQUAL "")
    message(FATAL_ERROR "Wrapper source not found in target SOURCES: '${_sources}'")
endif()

get_source_file_property(_defs "${_wrapper}" COMPILE_DEFINITIONS)
if(NOT _defs STREQUAL "DEMO_PROP=1")
    message(FATAL_ERROR "Wrapper COMPILE_DEFINITIONS mismatch. Expected 'DEMO_PROP=1', got '${_defs}'")
endif()

get_source_file_property(_opts "${_wrapper}" COMPILE_OPTIONS)
if(NOT _opts STREQUAL "-Wno-unused-parameter")
    message(FATAL_ERROR "Wrapper COMPILE_OPTIONS mismatch. Expected '-Wno-unused-parameter', got '${_opts}'")
endif()

get_source_file_property(_incs "${_wrapper}" INCLUDE_DIRECTORIES)
if(NOT _incs STREQUAL "${CMAKE_CURRENT_BINARY_DIR}")
    message(FATAL_ERROR "Wrapper INCLUDE_DIRECTORIES mismatch. Expected '${CMAKE_CURRENT_BINARY_DIR}', got '${_incs}'")
endif()

get_source_file_property(_flags "${_wrapper}" COMPILE_FLAGS)
if(NOT _flags STREQUAL "-Wno-unused-variable")
    message(FATAL_ERROR "Wrapper COMPILE_FLAGS mismatch. Expected '-Wno-unused-variable', got '${_flags}'")
endif()

get_source_file_property(_skip_unity "${_wrapper}" SKIP_UNITY_BUILD_INCLUSION)
if(NOT _skip_unity STREQUAL "ON")
    message(FATAL_ERROR "Wrapper SKIP_UNITY_BUILD_INCLUSION mismatch. Expected 'ON', got '${_skip_unity}'")
endif()
