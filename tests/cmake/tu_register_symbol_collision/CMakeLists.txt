cmake_minimum_required(VERSION 3.31)
project(gentest_tu_register_symbol_collision_fixture LANGUAGES CXX)

# The two source basenames intentionally share the same inner "tu_0042" fragment.
# parse_tu_index must not derive identical register symbols from that fragment.
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/a")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/b")

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/a/alpha_tu_0042_case.cpp"
  "[[using gentest: test(\"register_collision/alpha\")]]\n"
  "void alpha_tu_0042_case_fixture() {}\n")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/b/beta_tu_0042_case.cpp"
  "[[using gentest: test(\"register_collision/beta\")]]\n"
  "void beta_tu_0042_case_fixture() {}\n")

add_library(register_collision_obj OBJECT
    "${CMAKE_CURRENT_BINARY_DIR}/a/alpha_tu_0042_case.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/b/beta_tu_0042_case.cpp")

target_compile_features(register_collision_obj PRIVATE cxx_std_20)
