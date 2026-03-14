cmake_minimum_required(VERSION 3.31)
project(gentest_module_multiline_directives LANGUAGES CXX)

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

add_executable(multiline_directive_tests
    main.cpp)

target_sources(multiline_directive_tests
    PRIVATE
        FILE_SET multiline_cases TYPE CXX_MODULES FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/provider.ixx
            ${CMAKE_CURRENT_SOURCE_DIR}/consumer.cppm)

target_link_libraries(multiline_directive_tests PRIVATE gentest gentest_runtime)
target_compile_features(multiline_directive_tests PRIVATE cxx_std_20)

gentest_attach_codegen(multiline_directive_tests
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
