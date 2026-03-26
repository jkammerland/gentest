cmake_minimum_required(VERSION 3.31)
project(gentest VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Test executables in this repo are built as C++20 by default, but can be
# upgraded to C++23 to validate codegen + build wiring under newer language
# modes.
set(GENTEST_TEST_CXX_STANDARD "20" CACHE STRING "C++ standard used to build gentest's test executables (20 or 23).")
set_property(CACHE GENTEST_TEST_CXX_STANDARD PROPERTY STRINGS 20 23)

# Do not materialize implicit compiler include directories into the real build
# flags. That can break compiler-driven module dependency scanning on some
# Clang/libstdc++ toolchains. gentest_codegen already falls back to
# CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES directly when it needs system include
# visibility for compile_commands-driven analysis.

# Keep compile_commands.json free of experimental C++ modules flags so that
# external tools (clangd, static analyzers) don't choke on unknown options.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPERIMENTAL_CXX_MODULE_DYNDEP 0)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GentestDependencies)
include(GentestPublicModules)
gentest_ensure_fmt()

option(${PROJECT_NAME}_INSTALL "Install ${PROJECT_NAME} configuration" OFF)
option(${PROJECT_NAME}_BUILD_TESTING "Build the testing tree" ON)
option(GENTEST_USE_BOOST_JSON "Use Boost.JSON for Allure writer in generated runtime" OFF)
option(GENTEST_USE_BOOST_UUID "Use Boost.UUID for UUID generation in generated runtime" OFF)
set(GENTEST_ENABLE_PUBLIC_MODULES "AUTO" CACHE STRING "Enable gentest's public C++ module interfaces (AUTO, ON, or OFF)")
set_property(CACHE GENTEST_ENABLE_PUBLIC_MODULES PROPERTY STRINGS AUTO ON OFF)

if(NOT GENTEST_ENABLE_PUBLIC_MODULES MATCHES "^(AUTO|ON|OFF)$")
    message(FATAL_ERROR "GENTEST_ENABLE_PUBLIC_MODULES must be AUTO, ON, or OFF")
endif()

gentest_detect_public_module_support(_gentest_public_modules_supported _gentest_public_modules_reason)
if(GENTEST_ENABLE_PUBLIC_MODULES STREQUAL "OFF")
    set(_gentest_public_modules_enabled OFF)
    set(_gentest_public_modules_status "disabled explicitly by GENTEST_ENABLE_PUBLIC_MODULES=OFF")
elseif(GENTEST_ENABLE_PUBLIC_MODULES STREQUAL "ON")
    if(NOT _gentest_public_modules_supported)
        message(FATAL_ERROR
            "GENTEST_ENABLE_PUBLIC_MODULES=ON was requested, but the current toolchain cannot support gentest's public modules: ${_gentest_public_modules_reason}")
    endif()
    set(_gentest_public_modules_enabled ON)
    set(_gentest_public_modules_status "enabled explicitly by GENTEST_ENABLE_PUBLIC_MODULES=ON")
elseif(_gentest_public_modules_supported)
    set(_gentest_public_modules_enabled ON)
    set(_gentest_public_modules_status "enabled automatically")
else()
    set(_gentest_public_modules_enabled OFF)
    set(_gentest_public_modules_status "disabled automatically: ${_gentest_public_modules_reason}")
endif()

set(GENTEST_PUBLIC_MODULES_TOOLCHAIN_SUPPORTED "${_gentest_public_modules_supported}" CACHE INTERNAL
    "Whether the active generator/compiler can support gentest's public module interfaces" FORCE)
set(GENTEST_PUBLIC_MODULES_ENABLED "${_gentest_public_modules_enabled}" CACHE INTERNAL
    "Whether gentest exports its public module interfaces in this build" FORCE)
set(GENTEST_PUBLIC_MODULES_DISABLED_REASON "${_gentest_public_modules_reason}" CACHE INTERNAL
    "Why gentest's public module interfaces are unavailable in this build" FORCE)
message(STATUS "gentest: public named modules ${_gentest_public_modules_status}")
unset(_gentest_public_modules_supported)
unset(_gentest_public_modules_reason)
unset(_gentest_public_modules_enabled)
unset(_gentest_public_modules_status)

set(_gentest_build_codegen_default ON)
if(CMAKE_CROSSCOMPILING)
    set(_gentest_build_codegen_default OFF)
elseif(NOT ${PROJECT_NAME}_BUILD_TESTING)
    set(_gentest_build_codegen_default OFF)
endif()
option(GENTEST_BUILD_CODEGEN "Build the gentest code generator tool (gentest_codegen)" ${_gentest_build_codegen_default})
unset(_gentest_build_codegen_default)

if(GENTEST_BUILD_CODEGEN)
    add_subdirectory(tools)
endif()
include(GentestCodegen)
add_subdirectory(src)

if(${PROJECT_NAME}_INSTALL)
    include(FetchContent)
    FetchContent_Declare(
        target_install_package
        GIT_REPOSITORY https://github.com/jkammerland/target_install_package.cmake.git
        GIT_TAG v6.1.4
        FIND_PACKAGE_ARGS CONFIG QUIET
    )
    FetchContent_MakeAvailable(target_install_package)

    # Export a single 'gentest' package that contains both the interface target and the runtime library.
    target_install_package(gentest_runtime
        EXPORT_NAME ${PROJECT_NAME}
        PUBLIC_DEPENDENCIES "fmt CONFIG REQUIRED")
    target_install_package(gentest_main
        EXPORT_NAME ${PROJECT_NAME}
        PUBLIC_DEPENDENCIES "fmt CONFIG REQUIRED")
    target_install_package(${PROJECT_NAME}
        EXPORT_NAME ${PROJECT_NAME}
        INCLUDE_ON_FIND_PACKAGE
            "${PROJECT_SOURCE_DIR}/cmake/GentestConfigImportedConfigMap.cmake"
            "${PROJECT_SOURCE_DIR}/cmake/GentestCodegen.cmake"
        PUBLIC_DEPENDENCIES "fmt CONFIG REQUIRED")

    set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
    set(CPACK_PACKAGE_VENDOR "${PROJECT_NAME}")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    include(CPack)
else()
    message(STATUS "Enable install of ${PROJECT_NAME} with -D${PROJECT_NAME}_INSTALL=ON")
endif()

if(${PROJECT_NAME}_BUILD_TESTING)
    include(CTest)
    enable_testing()
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
    add_custom_target(gentest_codegen_all)
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
