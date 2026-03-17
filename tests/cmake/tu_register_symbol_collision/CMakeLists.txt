cmake_minimum_required(VERSION 3.31)
project(gentest_tu_register_symbol_collision_fixture LANGUAGES CXX)

# The two source basenames intentionally share the same inner "tu_0042" fragment.
# parse_tu_index must not derive identical register symbols from that fragment.
add_library(register_collision_obj OBJECT
    "${CMAKE_CURRENT_LIST_DIR}/a/alpha_tu_0042_case.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/b/beta_tu_0042_case.cpp")

target_compile_features(register_collision_obj PRIVATE cxx_std_20)
