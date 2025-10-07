cmake_minimum_required(VERSION 3.31)
project(gentest VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Keep compile_commands.json free of experimental C++ modules flags so that
# external tools (clangd, static analyzers) don't choke on unknown options.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPERIMENTAL_CXX_MODULE_DYNDEP 0)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

add_subdirectory(tools)
include(GentestCodegen)
add_subdirectory(src)

option(${PROJECT_NAME}_BUILD_TESTING "Build the testing tree" ON)
if(${PROJECT_NAME}_BUILD_TESTING)
    include(CTest)
    enable_testing()
    find_package(fmt CONFIG REQUIRED)

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
endif()

# Optional clang-tidy plugin (editor diagnostics) — default OFF
option(GENTEST_BUILD_TIDY_PLUGIN "Build gentest clang-tidy plugin" OFF)
if(GENTEST_BUILD_TIDY_PLUGIN)
    add_subdirectory(tools/tidy)
endif()
