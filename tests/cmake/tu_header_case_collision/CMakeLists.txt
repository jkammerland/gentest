cmake_minimum_required(VERSION 3.31)
project(gentest_tu_header_case_collision_fixture LANGUAGES CXX)

# Create sources in separate directories so this works on case-insensitive
# filesystems while still exercising case-folded output header collisions.
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/a")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/b")

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/a/lower_case.cpp" "int lower_case_fixture = 0;\n")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/b/Lower_Case.cpp" "int upper_case_fixture = 0;\n")

add_library(case_collision_obj OBJECT
    "${CMAKE_CURRENT_BINARY_DIR}/a/lower_case.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/b/Lower_Case.cpp")

target_compile_features(case_collision_obj PRIVATE cxx_std_20)
