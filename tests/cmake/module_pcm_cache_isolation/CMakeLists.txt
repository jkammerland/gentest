cmake_minimum_required(VERSION 3.31)
project(gentest_module_pcm_cache_isolation LANGUAGES CXX)

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

function(_gentest_add_pcm_cache_target target_name output_stem)
    set(multi_value_args MODULE_SOURCES)
    cmake_parse_arguments(GENTEST "" "" "${multi_value_args}" ${ARGN})

    add_executable(${target_name} main.cpp)
    target_sources(${target_name}
        PRIVATE
            FILE_SET module_cases TYPE CXX_MODULES FILES
                ${GENTEST_MODULE_SOURCES})
    target_link_libraries(${target_name} PRIVATE gentest gentest_runtime)
    target_compile_features(${target_name} PRIVATE cxx_std_20)
endfunction()

_gentest_add_pcm_cache_target(
    pcm_cache_dot_tests
    pcm_cache_dot_generated
    MODULE_SOURCES
      "${CMAKE_CURRENT_SOURCE_DIR}/alpha_dot_provider.cppm"
      "${CMAKE_CURRENT_SOURCE_DIR}/alpha_dot_consumer.cppm")

_gentest_add_pcm_cache_target(
    pcm_cache_underscore_tests
    pcm_cache_underscore_generated
    MODULE_SOURCES
      "${CMAKE_CURRENT_SOURCE_DIR}/alpha_underscore_provider.cppm"
      "${CMAKE_CURRENT_SOURCE_DIR}/alpha_underscore_consumer.cppm")
