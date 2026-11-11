include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/GentestCmakeFixtureTests.cmake")

# Configuration and compiler arguments for gentest package producers. The
# package consumer tests and the shared fixture prefix below both use them, so a
# producer they have in common is registered, built and installed only once.
function(gentest_package_test_producer_args out_config out_compiler_args out_module_compiler_args)
    set(_config "")
    if(WIN32)
        set(_config "RelWithDebInfo")
    elseif(CMAKE_CONFIGURATION_TYPES)
        set(_config "Debug")
    endif()

    set(_compiler_args "")
    if(DEFINED CMAKE_C_COMPILER AND NOT "${CMAKE_C_COMPILER}" STREQUAL "")
        list(APPEND _compiler_args -DPACKAGE_TEST_C_COMPILER=${CMAKE_C_COMPILER})
    endif()
    if(DEFINED CMAKE_CXX_COMPILER AND NOT "${CMAKE_CXX_COMPILER}" STREQUAL "")
        list(APPEND _compiler_args -DPACKAGE_TEST_CXX_COMPILER=${CMAKE_CXX_COMPILER})
    endif()
    if(DEFINED CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS
       AND NOT "${CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS}" STREQUAL ""
       AND NOT "${CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS}" MATCHES "-NOTFOUND$")
        list(APPEND _compiler_args
            -DPACKAGE_TEST_CXX_COMPILER_CLANG_SCAN_DEPS=${CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS})
    endif()

    set(_module_compiler_args ${_compiler_args})
    if(UNIX)
        gentest_resolve_clang_fixture_compilers(_module_c _module_cxx)
        if(NOT "${_module_c}" STREQUAL "" AND NOT "${_module_cxx}" STREQUAL "")
            set(_module_compiler_args
                -DPACKAGE_TEST_C_COMPILER=${_module_c}
                -DPACKAGE_TEST_CXX_COMPILER=${_module_cxx}
                -DPACKAGE_TEST_EXPECT_MODULES=ON)
            gentest_find_clang_scan_deps(_module_scan_deps "${_module_cxx}")
            if(NOT "${_module_scan_deps}" STREQUAL "")
                list(APPEND _module_compiler_args
                    -DPACKAGE_TEST_CXX_COMPILER_CLANG_SCAN_DEPS=${_module_scan_deps})
            endif()
        endif()
    endif()

    set(${out_config} "${_config}" PARENT_SCOPE)
    set(${out_compiler_args} "${_compiler_args}" PARENT_SCOPE)
    set(${out_module_compiler_args} "${_module_compiler_args}" PARENT_SCOPE)
endfunction()

# Fixture projects that only consume gentest share one installed gentest per
# toolchain variant instead of compiling gentest_runtime and fmt again through
# add_subdirectory(). The producer is an ordinary gentest package fixture
# (gentest_register_package_fixture), so with GENTEST_ENABLE_PACKAGE_TESTS=ON it
# is the same setup the package consumer tests already require. Variants:
#   MODULES  clang fixture compilers, public modules enabled when supported
#   CLANG    clang fixture compilers, public modules off
#   DEFAULT  the outer build's compilers, public modules off
#   NONE     no shared prefix: the fixture builds gentest from source
# Returns no arguments where the installed package cannot stand in for the
# fixture's own build: on Windows the producer uses the native LLVM CRT flags
# the fixtures do not, cross builds would build the host tool for the target,
# and multi-config outer builds pass fixtures no build type to match.
function(gentest_fixture_prefix_package_args out_var variant)
    set(${out_var} "" PARENT_SCOPE)
    if(variant STREQUAL "NONE" OR WIN32 OR CMAKE_CROSSCOMPILING OR CMAKE_CONFIGURATION_TYPES)
        return()
    endif()

    gentest_package_test_producer_args(_config _compiler_args _module_compiler_args)
    set(_args -DPACKAGE_NAME=${PROJECT_NAME} -DBUILD_CONFIG=${_config} -DPACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE=ON)
    if(variant STREQUAL "DEFAULT")
        list(APPEND _args -DPACKAGE_TEST_USE_MODULES=OFF ${_compiler_args})
    elseif(variant STREQUAL "MODULES" OR variant STREQUAL "CLANG")
        if(NOT ";${_module_compiler_args};" MATCHES ";-DPACKAGE_TEST_EXPECT_MODULES=ON;")
            # No clang fixture compilers: those fixtures skip.
            return()
        endif()
        set(_outer_cxx_arg "")
        foreach(_arg IN LISTS _compiler_args)
            if(_arg MATCHES "^-DPACKAGE_TEST_CXX_COMPILER=")
                set(_outer_cxx_arg "${_arg}")
            endif()
        endforeach()
        if(variant STREQUAL "MODULES")
            list(APPEND _args -DPACKAGE_TEST_USE_MODULES=ON ${_module_compiler_args})
        elseif(NOT _outer_cxx_arg STREQUAL "" AND _outer_cxx_arg IN_LIST _module_compiler_args)
            # The outer compiler is the clang the fixtures pick: share its producer.
            list(APPEND _args -DPACKAGE_TEST_USE_MODULES=OFF ${_compiler_args})
        else()
            list(FILTER _module_compiler_args EXCLUDE REGEX "^-DPACKAGE_TEST_EXPECT_MODULES=")
            list(APPEND _args -DPACKAGE_TEST_USE_MODULES=OFF ${_module_compiler_args})
        endif()
    else()
        message(FATAL_ERROR "Unknown fixture prefix variant '${variant}'")
    endif()
    set(${out_var} "${_args}" PARENT_SCOPE)
endfunction()

# _gentest_add_cmake_helper_test() for a fixture that consumes the shared
# prefix. The script receives the producer root as GENTEST_FIXTURE_PACKAGE_ROOT
# and decides with gentest_fixture_prefix_cache_args() whether the prefix
# matches the fixture's toolchain.
function(gentest_add_fixture_prefix_test variant name source_dir script_path label)
    gentest_fixture_prefix_package_args(_package_args "${variant}")
    set(_prefix_args "")
    if(_package_args)
        gentest_register_package_fixture(_fixture _root "${PROJECT_SOURCE_DIR}" ${_package_args})
        list(APPEND _prefix_args "-DGENTEST_FIXTURE_PACKAGE_ROOT=${_root}")
    endif()
    _gentest_add_cmake_helper_test("${name}" "${source_dir}" "${script_path}" "${label}" ${ARGN} ${_prefix_args})
    if(_package_args)
        set_property(TEST "${name}" APPEND PROPERTY FIXTURES_REQUIRED "${_fixture}")
    endif()
endfunction()
