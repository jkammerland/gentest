cmake_minimum_required(VERSION 3.31)
project(gentest_module_registration_rejects_non_interface LANGUAGES CXX)

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

add_executable(module_registration_rejects_non_interface
    main.cpp)

target_sources(module_registration_rejects_non_interface
    PRIVATE
        FILE_SET module_cases TYPE CXX_MODULES FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/cases.cppm)

target_link_libraries(module_registration_rejects_non_interface PRIVATE gentest gentest_runtime)
target_compile_features(module_registration_rejects_non_interface PRIVATE cxx_std_20)

gentest_register_module_tests(module_registration_rejects_non_interface
    FILE_SET module_cases
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
