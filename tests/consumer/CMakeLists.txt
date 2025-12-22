cmake_minimum_required(VERSION 3.25)
project(gentest_consumer LANGUAGES CXX)

find_package(gentest CONFIG REQUIRED)

add_executable(gentest_consumer main.cpp)
target_compile_features(gentest_consumer PRIVATE cxx_std_23)
target_link_libraries(gentest_consumer PRIVATE gentest::gentest)

