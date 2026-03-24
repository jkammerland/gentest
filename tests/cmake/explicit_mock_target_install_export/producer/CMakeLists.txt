cmake_minimum_required(VERSION 3.31)
project(gentest_explicit_mock_target_install_export_producer LANGUAGES CXX)

if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
    message(FATAL_ERROR "GENTEST_SOURCE_DIR must point at the gentest source tree")
endif()

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(gentest_INSTALL ON CACHE BOOL "" FORCE)
set(gentest_BUILD_TESTING OFF CACHE BOOL "" FORCE)
if(DEFINED GENTEST_CODEGEN_EXECUTABLE AND NOT "${GENTEST_CODEGEN_EXECUTABLE}" STREQUAL "")
    set(GENTEST_BUILD_CODEGEN OFF CACHE BOOL "" FORCE)
else()
    set(GENTEST_BUILD_CODEGEN ON CACHE BOOL "" FORCE)
endif()

add_subdirectory("${GENTEST_SOURCE_DIR}" gentest)

set(_fixture_support_include_dir "${CMAKE_CURRENT_BINARY_DIR}/fixture_support/include")
file(MAKE_DIRECTORY "${_fixture_support_include_dir}/fixture")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fixture_support/empty_install_root")
file(WRITE "${_fixture_support_include_dir}/fixture/generated_support.hpp"
"#pragma once\n\
namespace fixture {\n\
inline constexpr int kGeneratedSupportSentinel = 17;\n\
} // namespace fixture\n")

add_library(fixture_support INTERFACE)
target_include_directories(fixture_support INTERFACE
    "$<BUILD_INTERFACE:${_fixture_support_include_dir}>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/fixture_support>")

gentest_add_mocks(explicit_exported_mocks
    DEFS
        ${CMAKE_CURRENT_SOURCE_DIR}/header_mocks.hpp
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
    LINK_LIBRARIES fixture_support
    HEADER_NAME public/fixture_header_mocks.hpp)

gentest_add_mocks(explicit_module_exported_mocks
    DEFS
        ${CMAKE_CURRENT_SOURCE_DIR}/service_module.cppm
        ${CMAKE_CURRENT_SOURCE_DIR}/module_mocks.cppm
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated_module"
    LINK_LIBRARIES fixture_support
    MODULE_NAME fixture.explicit_module_mocks)

install(TARGETS explicit_exported_mocks
    EXPORT explicit_exported_mocksTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    FILE_SET gentest_explicit_mock_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

install(TARGETS fixture_support
    EXPORT explicit_exported_mocksTargets
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fixture_support/empty_install_root/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/fixture_support")

install(TARGETS explicit_module_exported_mocks
    EXPORT explicit_exported_mocksTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    FILE_SET gentest_explicit_mock_module_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILE_SET gentest_explicit_mock_modules_private DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILE_SET gentest_explicit_mock_aggregate_module DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/explicit_exported_mocksConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/explicit_exported_mocksConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/explicit_exported_mocks"
    NO_SET_AND_CHECK_MACRO
    NO_CHECK_REQUIRED_COMPONENTS_MACRO)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/explicit_exported_mocksConfig.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/explicit_exported_mocks")

install(EXPORT explicit_exported_mocksTargets
    NAMESPACE fixture::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/explicit_exported_mocks")
