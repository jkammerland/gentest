include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/scripts/PackageFixtureContext.cmake")

function(gentest_register_package_fixture out_fixture out_root source_dir)
    # Resolve exactly the inputs passed to the helper script. Consumer-only
    # choices are deliberately absent from the producer context's identity.
    set(SOURCE_DIR "${source_dir}")
    set(PACKAGE_TEST_USE_MODULES ON)
    set(PACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE "")
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
    # Consumers that bring a host tool do not need another installed generator.
    # Upgrade monotonically if any later consumer exercises the installed tool.
    if(NOT "${PACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE}" STREQUAL "ON")
        set_property(GLOBAL PROPERTY "${_fixture}_NEEDS_CODEGEN" ON)
    endif()

    get_property(_registered GLOBAL PROPERTY "${_fixture}_SETUP_ARGS" SET)
    if(NOT _registered)
        set_property(GLOBAL PROPERTY "${_fixture}_SETUP_ARGS"
            "${source_dir}" ${ARGN}
            "-DBUILD_ROOT=${PACKAGE_FIXTURE_BASE}"
            "-DPACKAGE_PRODUCER_ROOT=${_producer_root}")
        # Freeze the identity now; resolve capabilities after all consumers register.
        cmake_language(EVAL CODE "cmake_language(DEFER CALL _gentest_add_package_fixture_tests [[${_fixture}]])")
    endif()

    set(${out_fixture} "${_fixture}" PARENT_SCOPE)
    set(${out_root} "${_producer_root}" PARENT_SCOPE)
endfunction()

function(_gentest_add_package_fixture_tests fixture)
    get_property(_setup_args GLOBAL PROPERTY "${fixture}_SETUP_ARGS")
    get_property(_needs_codegen GLOBAL PROPERTY "${fixture}_NEEDS_CODEGEN")
    if(NOT _needs_codegen)
        set(_needs_codegen OFF)
    endif()
    list(POP_FRONT _setup_args _source_dir)
    set(_script "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/scripts/CheckPackageConsumer.cmake")
    foreach(_phase IN ITEMS SETUP CLEANUP)
        string(TOLOWER "${_phase}" _phase_lower)
        _gentest_add_cmake_helper_test("${fixture}_${_phase_lower}"
            "${_source_dir}" "${_script}" package ${_setup_args}
            "-DPACKAGE_TEST_PRODUCER_CODEGEN=${_needs_codegen}"
            "-DPACKAGE_TEST_PHASE=${_phase}")
    endforeach()
    set_tests_properties("${fixture}_setup" PROPERTIES FIXTURES_SETUP "${fixture}" COST 400)
    set_tests_properties("${fixture}_cleanup" PROPERTIES FIXTURES_CLEANUP "${fixture}")
endfunction()
