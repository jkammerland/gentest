cmake_minimum_required(VERSION 3.31)
project(gentest_explicit_mock_target_install_export_consumer LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(GENTEST_EXPLICIT_MOCK_THIRD_PARTY_ONLY "Only consume the third-party exported mock package" OFF)

if(GENTEST_EXPLICIT_MOCK_THIRD_PARTY_ONLY)
    find_package(explicit_third_party_exported_mocks CONFIG REQUIRED)

    if(TARGET gentest::gentest)
        message(FATAL_ERROR "Third-party exported mock package should not create gentest::gentest")
    endif()

    get_target_property(_third_party_links fixture::explicit_third_party_exported_mocks INTERFACE_LINK_LIBRARIES)
    if(_third_party_links)
        foreach(_third_party_link IN LISTS _third_party_links)
            if(_third_party_link MATCHES "^gentest::")
                message(FATAL_ERROR "Third-party exported mock package leaked '${_third_party_link}'")
            endif()
        endforeach()
    endif()

    add_executable(explicit_installed_third_party_consumer third_party_main.cpp)
    target_compile_features(explicit_installed_third_party_consumer PRIVATE cxx_std_20)
    target_link_libraries(explicit_installed_third_party_consumer PRIVATE fixture::explicit_third_party_exported_mocks)
    return()
endif()

find_package(explicit_exported_mocks CONFIG REQUIRED)
find_package(explicit_third_party_exported_mocks CONFIG REQUIRED)

add_executable(explicit_installed_consumer main.cpp)
target_compile_features(explicit_installed_consumer PRIVATE cxx_std_20)
target_link_libraries(explicit_installed_consumer PRIVATE fixture::explicit_exported_mocks)

add_executable(explicit_installed_module_consumer module_main.cpp)
target_compile_features(explicit_installed_module_consumer PRIVATE cxx_std_20)
target_link_libraries(explicit_installed_module_consumer PRIVATE fixture::explicit_module_exported_mocks)

add_executable(explicit_installed_module_codegen_consumer)
target_sources(explicit_installed_module_codegen_consumer
    PRIVATE
        FILE_SET installed_module_cases TYPE CXX_MODULES FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/module_cases.cppm)
target_compile_features(explicit_installed_module_codegen_consumer PRIVATE cxx_std_20)
target_link_libraries(explicit_installed_module_codegen_consumer
    PRIVATE
        gentest::gentest_main
        fixture::explicit_module_exported_mocks)
gentest_attach_codegen(explicit_installed_module_codegen_consumer
    MODULE_REGISTRATION
    FILE_SET installed_module_cases
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/codegen_consumer")

add_executable(explicit_installed_third_party_consumer third_party_main.cpp)
target_compile_features(explicit_installed_third_party_consumer PRIVATE cxx_std_20)
target_link_libraries(explicit_installed_third_party_consumer PRIVATE fixture::explicit_third_party_exported_mocks)
