cmake_minimum_required(VERSION 3.20)
project(gentest_scan_inspector LANGUAGES CXX)

add_executable(gentest_scan_inspector main.cpp)
target_compile_features(gentest_scan_inspector PRIVATE cxx_std_20)
target_include_directories(gentest_scan_inspector PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/../../tools/src")

set_target_properties(gentest_scan_inspector PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

if(CMAKE_CONFIGURATION_TYPES)
    foreach(_cfg ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER "${_cfg}" _cfg_upper)
        set_target_properties(gentest_scan_inspector PROPERTIES
            "RUNTIME_OUTPUT_DIRECTORY_${_cfg_upper}" "${CMAKE_BINARY_DIR}")
    endforeach()
endif()
