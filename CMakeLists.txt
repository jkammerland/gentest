cmake_minimum_required(VERSION 3.31)
project(gentest
    VERSION 1.0.0
    DESCRIPTION "C++20 testing framework with annotation-driven code generation"
    HOMEPAGE_URL "https://github.com/jkammerland/gentest"
    LANGUAGES C CXX)
set(gentest_SPDX_LICENSE "NOASSERTION")

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
include(GentestCompilerChecks)
include(GentestPublicModules)

option(${PROJECT_NAME}_INSTALL "Install ${PROJECT_NAME} configuration" OFF)
option(${PROJECT_NAME}_BUILD_TESTING "Build the testing tree" OFF)
option(GENTEST_ENABLE_CPS "Install experimental Common Package Specification metadata (requires CMake 4.3+)" OFF)
option(GENTEST_ENABLE_SBOM "Install experimental SPDX 3.0.1 SBOM metadata (requires CMake 4.3+)" OFF)
option(GENTEST_REQUIRE_PACKAGE_SIGNING "Fail package configuration unless a GPG signing key is provided" OFF)
set(GENTEST_GPG_SIGNING_KEY "" CACHE STRING "GPG fingerprint or key ID used to sign release packages")
set(GENTEST_GPG_PASSPHRASE_FILE "" CACHE FILEPATH "Optional file containing the GPG signing-key passphrase")
option(GENTEST_USE_BOOST_JSON "Use Boost.JSON for Allure writer in generated runtime" OFF)
option(GENTEST_USE_BOOST_UUID "Use Boost.UUID for UUID generation in generated runtime" OFF)
set(GENTEST_ENABLE_PUBLIC_MODULES "OFF" CACHE STRING "Enable gentest's public C++ module interfaces (AUTO, ON, or OFF)")
set_property(CACHE GENTEST_ENABLE_PUBLIC_MODULES PROPERTY STRINGS AUTO ON OFF)

gentest_ensure_fmt()
gentest_require_std_stop_token()

if(GENTEST_ENABLE_CPS)
    if(CMAKE_VERSION VERSION_LESS 4.3)
        message(FATAL_ERROR "GENTEST_ENABLE_CPS=ON requires CMake 4.3 or newer")
    endif()
    if(NOT DEFINED CMAKE_EXPERIMENTAL_EXPORT_PACKAGE_DEPENDENCIES
       OR CMAKE_EXPERIMENTAL_EXPORT_PACKAGE_DEPENDENCIES STREQUAL "")
        message(FATAL_ERROR
            "GENTEST_ENABLE_CPS=ON requires CMAKE_EXPERIMENTAL_EXPORT_PACKAGE_DEPENDENCIES to use this CMake version's activation value")
    endif()

    _gentest_resolve_fmt_target(_gentest_cps_fmt_target)
    set_property(TARGET "${_gentest_cps_fmt_target}" PROPERTY EXPORT_FIND_PACKAGE_NAME fmt)
    set(GENTEST_FMT_LINK_TARGET fmt::fmt)
    unset(_gentest_cps_fmt_target)
else()
    set(GENTEST_FMT_LINK_TARGET gentest::fmt_dependency)
endif()

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

if(CMAKE_CROSSCOMPILING)
    set(_gentest_build_codegen_default OFF)
else()
    set(_gentest_build_codegen_default "${${PROJECT_NAME}_BUILD_TESTING}")
    if(${PROJECT_NAME}_INSTALL)
        set(_gentest_build_codegen_default ON)
    endif()
endif()
option(GENTEST_BUILD_CODEGEN "Build the gentest code generator tool (gentest_codegen)" "${_gentest_build_codegen_default}")
unset(_gentest_build_codegen_default)

if(${PROJECT_NAME}_INSTALL AND NOT CMAKE_CROSSCOMPILING AND NOT GENTEST_BUILD_CODEGEN)
    message(FATAL_ERROR
        "${PROJECT_NAME}_INSTALL=ON requires GENTEST_BUILD_CODEGEN=ON so the installed CMake package includes "
        "the gentest_codegen host tool used by GentestCodegen.cmake.")
endif()

if(GENTEST_BUILD_CODEGEN)
    add_subdirectory(tools)
endif()
include(GentestCodegen)
add_subdirectory(src)

if(${PROJECT_NAME}_INSTALL)
    set(_gentest_target_install_package_root "${PROJECT_SOURCE_DIR}/third_party/target_install_package")
    find_package(target_install_package 7.0.8 EXACT CONFIG REQUIRED
        PATHS "${_gentest_target_install_package_root}"
        NO_DEFAULT_PATH)
    set(_gentest_fmt_public_dependency "fmt ${GENTEST_FMT_VERSION} EXACT CONFIG REQUIRED")

    set(_gentest_package_metadata_args)
    if(GENTEST_ENABLE_CPS)
        list(APPEND _gentest_package_metadata_args
            CPS
            CPS_PACKAGE_NAME "${PROJECT_NAME}"
            CPS_DESTINATION "share/cps/${PROJECT_NAME}"
            CPS_VERSION "${PROJECT_VERSION}"
            CPS_COMPAT_VERSION "${PROJECT_VERSION_MAJOR}.0.0"
            CPS_VERSION_SCHEMA "simple"
            CPS_DEFAULT_TARGETS "${PROJECT_NAME}"
            CPS_LICENSE "${gentest_SPDX_LICENSE}"
            CPS_DEFAULT_LICENSE "${gentest_SPDX_LICENSE}"
            CPS_DESCRIPTION "${PROJECT_DESCRIPTION}"
            CPS_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
    endif()
    if(GENTEST_ENABLE_SBOM)
        if(CMAKE_VERSION VERSION_LESS 4.3)
            message(FATAL_ERROR "GENTEST_ENABLE_SBOM=ON requires CMake 4.3 or newer")
        endif()
        list(APPEND _gentest_package_metadata_args
            SBOM
            SBOM_NAME "${PROJECT_NAME}"
            SBOM_DESTINATION "share/sbom/${PROJECT_NAME}"
            SBOM_VERSION "${PROJECT_VERSION}"
            SBOM_LICENSE "${gentest_SPDX_LICENSE}"
            SBOM_DESCRIPTION "${PROJECT_DESCRIPTION}"
            SBOM_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
    endif()

    # Export a single 'gentest' package that contains both the interface target and the runtime library.
    target_install_package(gentest_runtime
        EXPORT_NAME ${PROJECT_NAME}
        PUBLIC_DEPENDENCIES "${_gentest_fmt_public_dependency}"
        ${_gentest_package_metadata_args})
    target_install_package(gentest_main
        EXPORT_NAME ${PROJECT_NAME}
        PUBLIC_DEPENDENCIES "${_gentest_fmt_public_dependency}"
        ${_gentest_package_metadata_args})
    target_install_package(${PROJECT_NAME}
        EXPORT_NAME ${PROJECT_NAME}
        INCLUDE_ON_FIND_PACKAGE
            "${PROJECT_SOURCE_DIR}/cmake/GentestFmtDependency.cmake"
            "${PROJECT_SOURCE_DIR}/cmake/GentestCodegen.cmake"
        PUBLIC_DEPENDENCIES "${_gentest_fmt_public_dependency}"
        ${_gentest_package_metadata_args})

    if(GENTEST_ENABLE_SBOM AND TARGET gentest_codegen)
        install(SBOM gentest-tools
            EXPORT gentest-tools
            NO_PROJECT_METADATA
            DESTINATION "share/sbom/${PROJECT_NAME}"
            VERSION "${PROJECT_VERSION}"
            LICENSE "${gentest_SPDX_LICENSE}"
            DESCRIPTION "Gentest annotation scanner and code generator"
            HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
    endif()

    install(DIRECTORY "${PROJECT_SOURCE_DIR}/cmake/gentest/"
        DESTINATION "share/cmake/gentest/gentest"
        COMPONENT Development
        FILES_MATCHING PATTERN "*.cmake")
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/xmake/"
        DESTINATION "share/gentest/xmake"
        COMPONENT Development)
    install(FILES
            "${PROJECT_SOURCE_DIR}/README.md"
            "${PROJECT_SOURCE_DIR}/third_party/licenses/NOTICE.txt"
        DESTINATION "share/doc/${PROJECT_NAME}"
        COMPONENT Development)
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/docs/"
        DESTINATION "share/doc/${PROJECT_NAME}/docs"
        COMPONENT Development
        FILES_MATCHING
        PATTERN "*.md"
        PATTERN "stories" EXCLUDE)
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/third_party/licenses/"
        DESTINATION "share/licenses/${PROJECT_NAME}/third_party"
        COMPONENT Development
        FILES_MATCHING PATTERN "*.txt")

    set(_gentest_cpack_signing_args)
    if(GENTEST_GPG_SIGNING_KEY)
        list(APPEND _gentest_cpack_signing_args
            GPG_SIGNING_KEY "${GENTEST_GPG_SIGNING_KEY}"
            SIGNING_METHOD detached)
        if(GENTEST_GPG_PASSPHRASE_FILE)
            list(APPEND _gentest_cpack_signing_args
                GPG_PASSPHRASE_FILE "${GENTEST_GPG_PASSPHRASE_FILE}")
        endif()
    elseif(DEFINED ENV{GPG_SIGNING_KEY} AND NOT "$ENV{GPG_SIGNING_KEY}" STREQUAL "")
        list(APPEND _gentest_cpack_signing_args SIGNING_METHOD detached)
    elseif(GENTEST_REQUIRE_PACKAGE_SIGNING)
        message(FATAL_ERROR
            "GENTEST_REQUIRE_PACKAGE_SIGNING=ON requires GENTEST_GPG_SIGNING_KEY or the GPG_SIGNING_KEY environment variable")
    endif()

    export_cpack(
        PACKAGE_NAME "${PROJECT_NAME}"
        PACKAGE_VERSION "${PROJECT_VERSION}"
        PACKAGE_VENDOR "jkammerland"
        PACKAGE_DESCRIPTION "${PROJECT_DESCRIPTION}"
        PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}"
        PACKAGE_LICENSE "${gentest_SPDX_LICENSE}"
        GENERATORS TGZ ZIP
        COMPONENTS Development
        DEFAULT_COMPONENTS Development
        GENERATE_CHECKSUMS ON
        ${_gentest_cpack_signing_args}
        ADDITIONAL_CPACK_VARS
            CPACK_ARCHIVE_COMPONENT_INSTALL OFF
            CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
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
