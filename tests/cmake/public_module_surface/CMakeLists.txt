cmake_minimum_required(VERSION 3.31)
project(gentest_public_module_surface LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)

find_package(gentest CONFIG REQUIRED)

add_executable(public_module_surface main.cpp)
target_compile_features(public_module_surface PRIVATE cxx_std_20)
target_link_libraries(public_module_surface PRIVATE gentest::gentest)
