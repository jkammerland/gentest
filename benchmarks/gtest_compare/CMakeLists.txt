cmake_minimum_required(VERSION 3.24)

project(gentest_gtest_compare LANGUAGES C CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

get_filename_component(_gentest_compare_default_source_dir "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(GENTEST_COMPARE_SOURCE_DIR "${_gentest_compare_default_source_dir}" CACHE PATH "gentest source tree to benchmark")
unset(_gentest_compare_default_source_dir)

if(NOT EXISTS "${GENTEST_COMPARE_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "GENTEST_COMPARE_SOURCE_DIR does not point at a gentest source tree: ${GENTEST_COMPARE_SOURCE_DIR}")
endif()

set(gentest_BUILD_TESTING OFF CACHE BOOL "Do not build gentest's full test tree in the comparison harness" FORCE)
set(GENTEST_BUILD_CODEGEN OFF CACHE BOOL "Use a prebuilt gentest_codegen for the comparison harness" FORCE)

add_subdirectory("${GENTEST_COMPARE_SOURCE_DIR}" gentest EXCLUDE_FROM_ALL)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTEST REQUIRED IMPORTED_TARGET gtest gtest_main)

include(FetchContent)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "Disable Google Benchmark tests in the comparison harness" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "Disable Google Benchmark gtest tests in the comparison harness" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "Disable Google Benchmark install rules in the comparison harness" FORCE)
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "Disable Google Benchmark -Werror in the comparison harness" FORCE)
set(GENTEST_COMPARE_GBENCH_REPOSITORY "https://github.com/google/benchmark.git" CACHE STRING "Google Benchmark repository used by the comparison harness")
set(GENTEST_COMPARE_GBENCH_TAG "192ef10025eb2c4cdd392bc502f0c852196baa48" CACHE STRING "Google Benchmark commit, tag, or branch used by the comparison harness")
FetchContent_Declare(
    gbenchmark_compare
    GIT_REPOSITORY "${GENTEST_COMPARE_GBENCH_REPOSITORY}"
    GIT_TAG "${GENTEST_COMPARE_GBENCH_TAG}"
)

set(GENTEST_COMPARE_DOCTEST_REPOSITORY "https://github.com/jkammerland/doctest.git" CACHE STRING "doctest repository used by the comparison harness")
set(GENTEST_COMPARE_DOCTEST_TAG "397ce0086a7ba36524c5706dfc2820527260e65b" CACHE STRING "doctest commit, tag, or branch used by the comparison harness")
FetchContent_Declare(
    doctest_compare
    GIT_REPOSITORY "${GENTEST_COMPARE_DOCTEST_REPOSITORY}"
    GIT_TAG "${GENTEST_COMPARE_DOCTEST_TAG}"
)

set(GENTEST_COMPARE_NANOBENCH_REPOSITORY "https://github.com/martinus/nanobench.git" CACHE STRING "nanobench repository used by the comparison harness")
set(GENTEST_COMPARE_NANOBENCH_TAG "e4327893194f06928012eb81cabc606c4e4791ac" CACHE STRING "nanobench commit, tag, or branch used by the comparison harness")
FetchContent_Declare(
    nanobench_compare
    GIT_REPOSITORY "${GENTEST_COMPARE_NANOBENCH_REPOSITORY}"
    GIT_TAG "${GENTEST_COMPARE_NANOBENCH_TAG}"
)

FetchContent_MakeAvailable(gbenchmark_compare doctest_compare nanobench_compare)

set(_gentest_compare_sources
    "${GENTEST_COMPARE_SOURCE_DIR}/tests/unit/cases.cpp"
    "${GENTEST_COMPARE_SOURCE_DIR}/tests/integration/cases.cpp"
    "${GENTEST_COMPARE_SOURCE_DIR}/tests/skiponly/cases.cpp")

add_executable(gentest_compare_tests ${_gentest_compare_sources})
target_link_libraries(gentest_compare_tests PRIVATE gentest_main)
target_compile_features(gentest_compare_tests PRIVATE cxx_std_20)
target_include_directories(gentest_compare_tests PRIVATE
    "${GENTEST_COMPARE_SOURCE_DIR}/include"
    "${GENTEST_COMPARE_SOURCE_DIR}/tests")

gentest_attach_codegen(gentest_compare_tests
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/gentest_compare_generated"
    CLANG_ARGS
        -std=c++20
        "-I${GENTEST_COMPARE_SOURCE_DIR}/include"
        "-I${GENTEST_COMPARE_SOURCE_DIR}/tests")

add_executable(gentest_compare_benchmarks gentest_append_iota_bench.cpp)
target_link_libraries(gentest_compare_benchmarks PRIVATE gentest_main)
target_compile_features(gentest_compare_benchmarks PRIVATE cxx_std_20)
target_include_directories(gentest_compare_benchmarks PRIVATE
    "${GENTEST_COMPARE_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}")

gentest_attach_codegen(gentest_compare_benchmarks
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/gentest_compare_benchmark_generated"
    CLANG_ARGS
        -std=c++20
        "-I${GENTEST_COMPARE_SOURCE_DIR}/include"
        "-I${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(gtest_compare_tests
    gtest_unit.cpp
    gtest_integration.cpp
    gtest_skiponly.cpp)
target_link_libraries(gtest_compare_tests PRIVATE PkgConfig::GTEST)
target_link_libraries(gtest_compare_tests PRIVATE gentest_runtime)
target_compile_features(gtest_compare_tests PRIVATE cxx_std_20)
target_include_directories(gtest_compare_tests PRIVATE
    "${GENTEST_COMPARE_SOURCE_DIR}/include")

add_executable(gbench_compare_benchmarks gbench_append_iota_bench.cpp)
target_link_libraries(gbench_compare_benchmarks PRIVATE benchmark::benchmark_main)
target_compile_features(gbench_compare_benchmarks PRIVATE cxx_std_20)
target_include_directories(gbench_compare_benchmarks PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(doctest_compare_main STATIC doctest_main.cpp)
target_link_libraries(doctest_compare_main PUBLIC doctest::doctest)
target_compile_features(doctest_compare_main PUBLIC cxx_std_20)

add_executable(doctest_compare_tests
    doctest_unit.cpp
    doctest_integration.cpp
    doctest_skiponly.cpp)
target_link_libraries(doctest_compare_tests PRIVATE doctest_compare_main gentest_runtime)
target_compile_features(doctest_compare_tests PRIVATE cxx_std_20)
target_include_directories(doctest_compare_tests PRIVATE
    "${GENTEST_COMPARE_SOURCE_DIR}/include")

add_executable(nanobench_compare_benchmarks nanobench_append_iota_bench.cpp)
target_link_libraries(nanobench_compare_benchmarks PRIVATE nanobench::nanobench)
target_compile_features(nanobench_compare_benchmarks PRIVATE cxx_std_20)
target_include_directories(nanobench_compare_benchmarks PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

enable_testing()
add_test(NAME gentest_compare_tests COMMAND gentest_compare_tests)
add_test(NAME gtest_compare_tests COMMAND gtest_compare_tests)
add_test(NAME doctest_compare_tests COMMAND doctest_compare_tests)
add_test(NAME gentest_compare_benchmarks COMMAND gentest_compare_benchmarks
    --kind=bench
    --bench-epochs=1
    --bench-warmup=0
    --bench-min-epoch-time-s=0
    --bench-min-total-time-s=0
    --bench-max-total-time-s=10)
add_test(NAME gbench_compare_benchmarks COMMAND gbench_compare_benchmarks)
add_test(NAME nanobench_compare_benchmarks COMMAND nanobench_compare_benchmarks)

unset(_gentest_compare_sources)
