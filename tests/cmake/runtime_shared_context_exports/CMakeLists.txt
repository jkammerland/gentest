cmake_minimum_required(VERSION 3.31)
project(gentest_runtime_shared_context_exports LANGUAGES CXX)

if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
    message(FATAL_ERROR "GENTEST_SOURCE_DIR must point at the gentest source tree")
endif()

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(gentest_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(GENTEST_BUILD_CODEGEN OFF CACHE BOOL "" FORCE)
set(GENTEST_ENABLE_PUBLIC_MODULES OFF CACHE STRING "" FORCE)
set(GENTEST_RUNTIME_SHARED ON CACHE BOOL "" FORCE)

add_subdirectory("${GENTEST_SOURCE_DIR}" gentest)

add_executable(runtime_shared_context_consumer
    consumer.cpp)
target_link_libraries(runtime_shared_context_consumer PRIVATE gentest)
target_compile_features(runtime_shared_context_consumer PRIVATE cxx_std_20)
