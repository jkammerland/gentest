cmake_minimum_required(VERSION 3.31)
project(gentest_measured LANGUAGES CXX)

include(CTest)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(gentest CONFIG REQUIRED)

add_executable(gentest_measured cases.hpp)
target_compile_features(gentest_measured PRIVATE cxx_std_20)
target_link_libraries(gentest_measured PRIVATE gentest::gentest_main)

gentest_attach_codegen(gentest_measured
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/gentest_codegen")
# CTest uses short smoke settings; run the executable directly for measurements.
gentest_discover_tests(gentest_measured EXTRA_ARGS
    --bench-epochs=3 --bench-warmup=1
    --bench-min-epoch-time-s=0.0001 --bench-min-total-time-s=0
    --bench-max-total-time-s=0.02)
