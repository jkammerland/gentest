cmake_minimum_required(VERSION 3.31)
project(gentest_public_module_surface LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(gentest CONFIG REQUIRED)

add_executable(public_module_surface main.cpp)
target_compile_features(public_module_surface PRIVATE cxx_std_20)
target_sources(public_module_surface
    PRIVATE
        FILE_SET module_cases TYPE CXX_MODULES FILES
            cases.cppm)
target_link_libraries(public_module_surface PRIVATE gentest::gentest)

gentest_attach_codegen(public_module_surface
    MODULE_REGISTRATION
    FILE_SET module_cases
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/gentest_codegen")

function(gentest_add_public_module_hidden_probe target source)
    add_executable(${target} EXCLUDE_FROM_ALL ${source})
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_link_libraries(${target} PRIVATE gentest::gentest)
endfunction()

gentest_add_public_module_hidden_probe(public_module_hides_register_cases hidden_register_cases.cpp)
gentest_add_public_module_hidden_probe(public_module_hides_snapshot_registered_cases hidden_snapshot_registered_cases.cpp)
gentest_add_public_module_hidden_probe(public_module_hides_shared_fixture_scope hidden_shared_fixture_scope.cpp)
gentest_add_public_module_hidden_probe(public_module_hides_register_shared_fixture hidden_register_shared_fixture.cpp)
