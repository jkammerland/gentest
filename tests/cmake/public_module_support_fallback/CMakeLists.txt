cmake_minimum_required(VERSION 3.31)
project(gentest_public_module_support_fallback LANGUAGES CXX)

if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
    message(FATAL_ERROR "GENTEST_SOURCE_DIR must point at the gentest source tree")
endif()

set(CMAKE_CXX_EXTENSIONS OFF)
set(gentest_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(GENTEST_BUILD_CODEGEN OFF CACHE BOOL "" FORCE)

add_subdirectory("${GENTEST_SOURCE_DIR}" gentest)

if(GENTEST_PUBLIC_MODULES_ENABLED)
    message(FATAL_ERROR "Expected gentest public modules to be disabled in this fallback fixture")
endif()

get_target_property(_module_sets gentest CXX_MODULE_SETS)
if(DEFINED _module_sets
   AND NOT _module_sets STREQUAL "NOTFOUND"
   AND NOT _module_sets STREQUAL "")
    message(FATAL_ERROR "gentest unexpectedly still publishes CXX_MODULES in fallback mode: ${_module_sets}")
endif()

add_executable(fallback_consumer
    trivial.cpp)

target_link_libraries(fallback_consumer PRIVATE gentest_main)
target_compile_features(fallback_consumer PRIVATE cxx_std_20)
set_property(TARGET fallback_consumer PROPERTY CXX_SCAN_FOR_MODULES OFF)
