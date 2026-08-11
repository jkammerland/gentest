include_guard(GLOBAL)

# Factory registration mode for gentest.
#
# Generates per-source companion .cpp files with factory functions,
# and a single registry .cpp that calls all factories from main().
# No static initializers — everything happens explicitly.
#
# Usage:
#   gentest_attach_factory_codegen(my_tests)
#
# This replaces gentest_attach_codegen() for projects that want the
# factory registration model.

# Generate factory registration files for a target.
# Generates:
#   - One companion .cpp per source (e.g., cases.gentest.cpp)
#   - One registry .cpp (gentest_registry.gentest.cpp)
function(gentest_attach_factory_codegen target)
    # Resolve codegen executable
    if(NOT TARGET gentest_codegen AND NOT DEFINED GENTEST_CODEGEN_EXECUTABLE)
        message(FATAL_ERROR
            "gentest_attach_factory_codegen(${target}): no gentest code generator available. "
            "Configure with -DGENTEST_BUILD_CODEGEN=ON or -DGENTEST_CODEGEN_EXECUTABLE=<path>.")
    endif()

    _gentest_resolve_codegen_backend(
        TARGET ${target}
        OUT_CODEGEN_TARGET _gentest_codegen_target
        OUT_CODEGEN_EXECUTABLE _gentest_codegen_executable
        OUT_CODEGEN_BACKEND_KIND _gentest_codegen_backend_kind)

    # Collect sources from target
    get_target_property(_gentest_target_sources ${target} SOURCES)
    if(NOT _gentest_target_sources)
        message(FATAL_ERROR "gentest_attach_factory_codegen(${target}): target has no SOURCES")
    endif()

    # Filter to .cpp and .cppm files (skip generated files, interface libs, etc.)
    set(_gentest_factory_sources "")
    foreach(_gentest_src IN LISTS _gentest_target_sources)
        if("${_gentest_src}" MATCHES "\\$<")
            continue()
        endif()
        get_filename_component(_gentest_ext "${_gentest_src}" EXT)
        if("${_gentest_ext}" STREQUAL ".cpp" OR "${_gentest_ext}" STREQUAL ".cppm" OR
           "${_gentest_ext}" STREQUAL ".cc" OR "${_gentest_ext}" STREQUAL ".cxx")
            list(APPEND _gentest_factory_sources "${_gentest_src}")
        endif()
    endforeach()

    if(_gentest_factory_sources STREQUAL "")
        message(WARNING "gentest_attach_factory_codegen(${target}): no test sources found in target")
        return()
    endif()

    # Set up output directory
    if(NOT DEFINED GENTEST_FACTORY_OUTPUT_DIR)
        set(GENTEST_FACTORY_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/gentest_factory/${target}")
    endif()

    # Collect clang args from target properties
    set(_gentest_clang_args "")
    get_target_property(_gentest_includes ${target} INCLUDE_DIRECTORIES)
    if(_gentest_includes)
        foreach(_inc IN LISTS _gentest_includes)
            list(APPEND _gentest_clang_args "-I${_inc}")
        endforeach()
    endif()
    get_target_property(_gentest_compile_defs ${target} COMPILE_DEFINITIONS)
    if(_gentest_compile_defs)
        foreach(_def IN LISTS _gentest_compile_defs)
            list(APPEND _gentest_clang_args "-D${_def}")
        endforeach()
    endif()
    get_target_property(_gentest_compile_opts ${target} COMPILE_OPTIONS)
    if(_gentest_compile_opts)
        list(APPEND _gentest_clang_args ${_gentest_compile_opts})
    endif()

    # Add C++ standard flag
    get_target_property(_gentest_std ${target} CXX_STANDARD)
    if(NOT _gentest_std OR _gentest_std MATCHES "-NOTFOUND$")
        set(_gentest_std 20)
    endif()
    list(APPEND _gentest_clang_args "-std=c++${_gentest_std}")

    # Generate the stamp file (tracks when codegen needs to re-run)
    set(_gentest_stamp_file "${GENTEST_FACTORY_OUTPUT_DIR}/.gentest_factory_stamp")
    file(MAKE_DIRECTORY "${GENTEST_FACTORY_OUTPUT_DIR}")

    # Build the codegen command
    set(_gentest_codegen_cmd ${_gentest_codegen_executable})
    list(APPEND _gentest_codegen_cmd --factory-output-dir "${GENTEST_FACTORY_OUTPUT_DIR}")
    list(APPEND _gentest_codegen_cmd --compdb "${CMAKE_BINARY_DIR}")
    list(APPEND _gentest_codegen_cmd -- ${_gentest_clang_args})
    list(APPEND _gentest_codegen_cmd ${_gentest_factory_sources})

    # Create custom command to run codegen
    add_custom_command(
        OUTPUT "${_gentest_stamp_file}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${GENTEST_FACTORY_OUTPUT_DIR}"
        COMMAND ${_gentest_codegen_cmd}
        COMMAND ${CMAKE_COMMAND} -E touch "${_gentest_stamp_file}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "gentest: generating factory registration for ${target}"
        VERBATIM
    )

    # Create a custom target that depends on codegen output
    add_custom_target(gentest_factory_${target}_codegen
        DEPENDS "${_gentest_stamp_file}"
    )

    # Find generated .cpp files
    file(GLOB _gentest_generated_cpp "${GENTEST_FACTORY_OUTPUT_DIR}/*.gentest.cpp")

    # Add generated files to target
    target_sources(${target} PRIVATE ${_gentest_generated_cpp})

    # Add dependency on codegen
    add_dependencies(${target} gentest_factory_${target}_codegen)

    # If gentest_codegen_all exists, add to it
    if(TARGET gentest_codegen_all)
        add_dependencies(gentest_codegen_all gentest_factory_${target}_codegen)
    endif()

    # Set include directories for generated files
    get_target_property(_gentest_include_dirs ${target} INCLUDE_DIRECTORIES)
    if(_gentest_include_dirs)
        target_include_directories(${target} PRIVATE ${_gentest_include_dirs})
    endif()

    # Store generated files for later use
    set_property(TARGET ${target} PROPERTY GENTEST_FACTORY_OUTPUT_DIR "${GENTEST_FACTORY_OUTPUT_DIR}")
    set_property(TARGET ${target} PROPERTY GENTEST_FACTORY_GENERATED_FILES "${_gentest_generated_cpp}")
    set_property(TARGET ${target} PROPERTY GENTEST_FACTORY_CODEGEN_DEP_TARGET gentest_factory_${target}_codegen)

    unset(_gentest_codegen_cmd)
    unset(_gentest_clang_args)
    unset(_gentest_generated_cpp)
    unset(_gentest_factory_sources)
    unset(_gentest_std)
    unset(_gentest_includes)
    unset(_gentest_compile_defs)
    unset(_gentest_compile_opts)
    unset(_gentest_target_sources)
    unset(_gentest_codegen_target)
    unset(_gentest_codegen_executable)
    unset(_gentest_codegen_backend_kind)
    unset(_gentest_stamp_file)
endfunction()
