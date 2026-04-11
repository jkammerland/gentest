cmake_minimum_required(VERSION 3.31)
project(gentest_module_registration_same_module_impl LANGUAGES CXX)

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

add_executable(module_registration_same_module_impl
    main.cpp)

set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/cases.cppm PROPERTIES
    COMPILE_DEFINITIONS "GENTEST_SOURCE_LEVEL_CASE_DEFINE=1")

target_sources(module_registration_same_module_impl
    PRIVATE
        FILE_SET support_modules TYPE CXX_MODULES FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/support.cppm
    PRIVATE
        FILE_SET module_cases TYPE CXX_MODULES FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/cases.cppm)

target_link_libraries(module_registration_same_module_impl PRIVATE gentest gentest_runtime)
target_compile_features(module_registration_same_module_impl PRIVATE cxx_std_20)

gentest_register_module_tests(module_registration_same_module_impl
    FILE_SET module_cases
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")

set(_registration_impl "${CMAKE_CURRENT_BINARY_DIR}/generated/tu_0000_cases.registration.gentest.cpp")
get_source_file_property(_registration_impl_defs "${_registration_impl}" COMPILE_DEFINITIONS)
list(FIND _registration_impl_defs "GENTEST_SOURCE_LEVEL_CASE_DEFINE=1" _registration_impl_define_idx)
if(_registration_impl_define_idx EQUAL -1)
    message(FATAL_ERROR
        "Expected clean additive registration implementation '${_registration_impl}' to mirror source-level COMPILE_DEFINITIONS "
        "from cases.cppm")
endif()
