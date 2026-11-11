# Included by fixture projects under tests/cmake/*. Fixtures whose assertions are
# not about building gentest itself consume a shared installed gentest when the
# driver script passes GENTEST_FIXTURE_PREFIX (see tests/cmake/FixturePrefix.cmake);
# otherwise, e.g. when a fixture is configured by hand, they build gentest from
# source as before. Fixtures link the gentest:: names, which both modes provide.
include_guard(DIRECTORY)

# A multi-config fixture generates every configuration, but the shared prefix
# only provides one. Map the other configurations to it, only for imported
# targets that provide the prefix configuration (gentest, and fmt when the
# producer installed it), so unrelated imported targets such as a system fmt
# with a Release or None configuration are left alone.
function(gentest_fixture_map_prefix_configs prefix_config)
    string(TOUPPER "${prefix_config}" prefix_config_upper)
    get_directory_property(imported_targets IMPORTED_TARGETS)
    foreach(imported_target IN LISTS imported_targets)
        get_target_property(target_configs "${imported_target}" IMPORTED_CONFIGURATIONS)
        if(NOT target_configs OR NOT prefix_config_upper IN_LIST target_configs)
            continue()
        endif()
        foreach(config IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
            if(NOT config IN_LIST target_configs)
                set_property(TARGET "${imported_target}" PROPERTY MAP_IMPORTED_CONFIG_${config} "${prefix_config}")
            endif()
        endforeach()
    endforeach()
endfunction()

macro(gentest_fixture_add_gentest binary_dir)
    if(DEFINED GENTEST_FIXTURE_PREFIX AND NOT "${GENTEST_FIXTURE_PREFIX}" STREQUAL "")
        # The prefix also carries fmt when the producer fetched it.
        list(PREPEND CMAKE_PREFIX_PATH "${GENTEST_FIXTURE_PREFIX}")
        find_package(gentest CONFIG REQUIRED PATHS "${GENTEST_FIXTURE_PREFIX}" NO_DEFAULT_PATH)
        message(STATUS "gentest fixture: using the shared prebuilt gentest from '${GENTEST_FIXTURE_PREFIX}'")
        if(DEFINED GENTEST_FIXTURE_PREFIX_CONFIG AND NOT "${GENTEST_FIXTURE_PREFIX_CONFIG}" STREQUAL "")
            gentest_fixture_map_prefix_configs("${GENTEST_FIXTURE_PREFIX_CONFIG}")
        endif()
    else()
        add_subdirectory("${GENTEST_SOURCE_DIR}" "${binary_dir}" ${ARGN})
    endif()
endmacro()
