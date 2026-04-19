cmake_minimum_required(VERSION 3.31)
project(gentest_genex_compile_definitions LANGUAGES CXX)

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
set(GENTEST_ENABLE_PUBLIC_MODULES OFF CACHE STRING "" FORCE)

add_subdirectory("${GENTEST_SOURCE_DIR}" gentest)

add_executable(genex_compile_definitions)
target_link_libraries(genex_compile_definitions PRIVATE gentest::gentest_main)
target_compile_features(genex_compile_definitions PRIVATE cxx_std_20)
target_compile_definitions(genex_compile_definitions PRIVATE
    $<$<CONFIG:Debug>:GENTEST_TARGET_DEBUG=1>
    $<$<CONFIG:Release>:GENTEST_TARGET_RELEASE=1>)
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/cases.cpp PROPERTIES
    COMPILE_DEFINITIONS "$<$<CONFIG:Debug>:GENTEST_SOURCE_DEBUG=1>;$<$<CONFIG:Release>:GENTEST_SOURCE_RELEASE=1>")

if(CMAKE_CONFIGURATION_TYPES)
    gentest_attach_codegen(genex_compile_definitions
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/genex_compile_definitions.cpp"
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/cases.cpp)
else()
    gentest_attach_codegen(genex_compile_definitions
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/cases.cpp)
endif()
