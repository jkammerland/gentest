cmake_minimum_required(VERSION 3.31)
project(gentest VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Export implicit compiler include directories to compile_commands.json
# This makes system headers visible to tools like clangd and our gentest_codegen
# without requiring runtime compiler queries
set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES ${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES})

# Keep compile_commands.json free of experimental C++ modules flags so that
# external tools (clangd, static analyzers) don't choke on unknown options.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPERIMENTAL_CXX_MODULE_DYNDEP 0)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

add_subdirectory(tools)
include(GentestCodegen)
add_subdirectory(src)

option(${PROJECT_NAME}_BUILD_TESTING "Build the testing tree" ON)
option(GENTEST_USE_BOOST_JSON "Use Boost.JSON for Allure writer in generated runtime" OFF)
option(GENTEST_USE_BOOST_UUID "Use Boost.UUID for UUID generation in generated runtime" OFF)

set(_gentest_boost_components "")
if(GENTEST_USE_BOOST_JSON)
    list(APPEND _gentest_boost_components json)
endif()
if(GENTEST_USE_BOOST_UUID)
    list(APPEND _gentest_boost_components uuid)
endif()
if(_gentest_boost_components)
    find_package(Boost CONFIG REQUIRED COMPONENTS ${_gentest_boost_components})
endif()
if(${PROJECT_NAME}_BUILD_TESTING)
    include(CTest)
    enable_testing()
    find_package(fmt CONFIG REQUIRED)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)

    # Optional: libassert integration tests
    option(GENTEST_WITH_LIBASSERT "Enable libassert integration tests" OFF)
    option(GENTEST_LIBASSERT_FIND_PACKAGE "Use find_package(libassert) instead of FetchContent" OFF)
    option(GENTEST_WITH_LIBASSERT_NOEXCEPT_TESTS "Add a -fno-exceptions death test for libassert integration" OFF)
    if(GENTEST_WITH_LIBASSERT)
        if(GENTEST_LIBASSERT_FIND_PACKAGE)
            find_package(libassert CONFIG REQUIRED)
        else()
            include(FetchContent)
            FetchContent_Declare(
                libassert
                GIT_REPOSITORY https://github.com/jeremy-rifkin/libassert.git
                GIT_TAG main
            )
            FetchContent_MakeAvailable(libassert)
        endif()
    endif()

    add_subdirectory(tools/core_tests)
    add_subdirectory(tests)

    # Compile time benchmark targets
    add_custom_target(bench-compile
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/bench_compile.py
                --build-dir ${CMAKE_BINARY_DIR}
                --jobs 1
        USES_TERMINAL
        COMMENT "Benchmark compile times (generator + test targets). Clean-first by default")

    add_custom_target(bench-compile-no-clean
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/bench_compile.py
                --build-dir ${CMAKE_BINARY_DIR}
                --jobs 1
                --no-clean
        USES_TERMINAL
        COMMENT "Benchmark compile times without clean (incremental)")

    # Release benchmark (requires a configured release preset build dir)
    add_custom_target(bench-compile-release
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/bench_compile.py
                --preset release
                --jobs 1
        USES_TERMINAL
        COMMENT "Benchmark compile times for Release preset (generator + test targets)")

    add_custom_target(bench-compile-release-no-clean
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/bench_compile.py
                --preset release
                --jobs 1
                --no-clean
        USES_TERMINAL
        COMMENT "Benchmark compile times for Release preset without clean (incremental)")

    # Compare benchmark between Debug (current build dir) and Release preset
    add_custom_target(bench-compare
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/bench_compare.py
                --a-build-dir ${CMAKE_BINARY_DIR}
                --b-preset release
                --jobs 1
                --no-clean
        USES_TERMINAL
        COMMENT "Compare compile times: current build-dir (A) vs release preset (B)")
endif()

# Optional clang-tidy plugin (editor diagnostics) — default OFF
option(GENTEST_BUILD_TIDY_PLUGIN "Build gentest clang-tidy plugin" OFF)
if(GENTEST_BUILD_TIDY_PLUGIN)
    add_subdirectory(tools/tidy)
endif()
