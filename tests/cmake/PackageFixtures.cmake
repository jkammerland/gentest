include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/scripts/PackageFixtureContext.cmake")

function(gentest_register_package_fixture out_fixture out_root source_dir)
    # Resolve exactly the inputs passed to the helper script. Consumer-only
    # choices are deliberately absent from the producer context's identity.
    set(SOURCE_DIR "${source_dir}")
    set(PACKAGE_TEST_USE_MODULES ON)
    set(GENERATOR "${CMAKE_GENERATOR}")
    set(GENERATOR_PLATFORM "${CMAKE_GENERATOR_PLATFORM}")
    set(GENERATOR_TOOLSET "${CMAKE_GENERATOR_TOOLSET}")
    set(TOOLCHAIN_FILE "${CMAKE_TOOLCHAIN_FILE}")
    set(MAKE_PROGRAM "${CMAKE_MAKE_PROGRAM}")
    set(C_COMPILER "${CMAKE_C_COMPILER}")
    set(CXX_COMPILER "${CMAKE_CXX_COMPILER}")
    set(BUILD_TYPE "${CMAKE_BUILD_TYPE}")
    foreach(_argument IN LISTS ARGN)
        if(_argument MATCHES "^-D([^:=]+)(:[^=]+)?=(.*)$")
            set("${CMAKE_MATCH_1}" "${CMAKE_MATCH_3}")
        else()
            message(FATAL_ERROR "Unexpected package fixture argument: ${_argument}")
        endif()
    endforeach()
    set(PACKAGE_FIXTURE_BASE "${CMAKE_CURRENT_BINARY_DIR}/package_fixtures")
    gentest_package_fixture_context()
    set(_fixture "gentest_package_${_producer_hash_short}")
    set(_setup "${_fixture}_setup")
    set(_cleanup "${_fixture}_cleanup")

    if(NOT TEST "${_setup}")
        set(_script "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/scripts/CheckPackageConsumer.cmake")
        foreach(_phase IN ITEMS SETUP CLEANUP)
            string(TOLOWER "${_phase}" _phase_lower)
            _gentest_add_cmake_helper_test("${_fixture}_${_phase_lower}"
                "${source_dir}" "${_script}" package ${ARGN}
                "-DBUILD_ROOT=${PACKAGE_FIXTURE_BASE}"
                "-DPACKAGE_PRODUCER_ROOT=${_producer_root}"
                "-DPACKAGE_TEST_PHASE=${_phase}")
        endforeach()
        set_tests_properties("${_setup}" PROPERTIES FIXTURES_SETUP "${_fixture}")
        set_tests_properties("${_cleanup}" PROPERTIES FIXTURES_CLEANUP "${_fixture}")
    endif()

    set(${out_fixture} "${_fixture}" PARENT_SCOPE)
    set(${out_root} "${_producer_root}" PARENT_SCOPE)
endfunction()
