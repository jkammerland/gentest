cmake_minimum_required(VERSION 3.31)
project(gentest_module_header_unit_import_preamble LANGUAGES CXX)

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

gentest_add_mocks(header_unit_import_preamble_mocks
    DEFS
        ${CMAKE_CURRENT_SOURCE_DIR}/provider.cppm
        ${CMAKE_CURRENT_SOURCE_DIR}/mock_defs.cppm
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/mocks"
    MODULE_NAME gentest.header_unit_import_preamble_mocks)

add_executable(header_unit_import_preamble_tests
    main.cpp)

target_sources(header_unit_import_preamble_tests
    PRIVATE
        FILE_SET header_unit_cases TYPE CXX_MODULES FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/cases.cppm)

target_link_libraries(header_unit_import_preamble_tests PRIVATE gentest gentest_runtime)
gentest_link_mocks(header_unit_import_preamble_tests PRIVATE header_unit_import_preamble_mocks)
target_compile_features(header_unit_import_preamble_tests PRIVATE cxx_std_20)

gentest_attach_codegen(header_unit_import_preamble_tests
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
