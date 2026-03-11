cmake_minimum_required(VERSION 3.31)
project(gentest_module_mock_imported_sibling LANGUAGES CXX)

if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
    message(FATAL_ERROR "GENTEST_SOURCE_DIR must point at the gentest source tree")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(gentest_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(GENTEST_BUILD_CODEGEN ON CACHE BOOL "" FORCE)

add_subdirectory("${GENTEST_SOURCE_DIR}" gentest)

add_executable(imported_sibling_tests
    main.cpp)

target_sources(imported_sibling_tests
    PRIVATE
        FILE_SET imported_sibling_cases TYPE CXX_MODULES FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/provider.cppm
            ${CMAKE_CURRENT_SOURCE_DIR}/consumer.cppm)

target_link_libraries(imported_sibling_tests PRIVATE gentest_main gentest_runtime)
target_compile_features(imported_sibling_tests PRIVATE cxx_std_20)

gentest_attach_codegen(imported_sibling_tests
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
