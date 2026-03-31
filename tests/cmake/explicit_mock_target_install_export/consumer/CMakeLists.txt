cmake_minimum_required(VERSION 3.31)
project(gentest_explicit_mock_target_install_export_consumer LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(explicit_exported_mocks CONFIG REQUIRED)

add_executable(explicit_installed_consumer main.cpp)
target_compile_features(explicit_installed_consumer PRIVATE cxx_std_20)
target_link_libraries(explicit_installed_consumer PRIVATE fixture::explicit_exported_mocks)

add_executable(explicit_installed_module_consumer module_main.cpp)
target_compile_features(explicit_installed_module_consumer PRIVATE cxx_std_20)
target_link_libraries(explicit_installed_module_consumer PRIVATE fixture::explicit_module_exported_mocks)

add_executable(explicit_installed_module_codegen_consumer module_cases.cppm)
target_compile_features(explicit_installed_module_codegen_consumer PRIVATE cxx_std_20)
target_link_libraries(explicit_installed_module_codegen_consumer
    PRIVATE
        gentest::gentest_main
        fixture::explicit_module_exported_mocks)
gentest_attach_codegen(explicit_installed_module_codegen_consumer
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/codegen_consumer")
