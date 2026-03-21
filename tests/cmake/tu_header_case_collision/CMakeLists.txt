cmake_minimum_required(VERSION 3.31)
project(gentest_tu_header_case_collision_fixture LANGUAGES CXX)

add_library(case_collision_obj OBJECT
    "${CMAKE_CURRENT_LIST_DIR}/a/lower_case.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/b/Lower_Case.cpp")

target_compile_features(case_collision_obj PRIVATE cxx_std_20)
