include_guard(GLOBAL)

if(NOT DEFINED GENTEST_CODEGEN_EXECUTABLE)
    set(GENTEST_CODEGEN_EXECUTABLE "" CACHE FILEPATH
        "Path to a host-built gentest_codegen executable used when the in-tree gentest_codegen target is unavailable (e.g. cross-compiling).")
endif()

if(NOT DEFINED GENTEST_CODEGEN_TARGET)
    set(GENTEST_CODEGEN_TARGET "" CACHE STRING
        "CMake target name that produces a runnable gentest_codegen executable (alternative to GENTEST_CODEGEN_EXECUTABLE).")
endif()

function(gentest_attach_codegen target)
    set(options NO_INCLUDE_SOURCES STRICT_FIXTURE QUIET_CLANG)
    set(one_value_args OUTPUT ENTRY)
    set(multi_value_args SOURCES CLANG_ARGS DEPENDS)
    cmake_parse_arguments(GENTEST "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT GENTEST_SOURCES)
        message(FATAL_ERROR "gentest_attach_codegen requires SOURCES")
    endif()

    if(NOT GENTEST_ENTRY)
        set(GENTEST_ENTRY gentest::run_all_tests)
    endif()

    if(NOT GENTEST_OUTPUT)
        set(GENTEST_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${target}_generated.cpp")
    endif()

    # Configure-time collision checks: prevent multiple targets (or multiple
    # calls) from writing to the same generated OUTPUT file, which would
    # silently clobber results during the build.
    if("${GENTEST_OUTPUT}" MATCHES "\\$<")
        message(WARNING "gentest_attach_codegen(${target}): OUTPUT contains generator expressions; collision checks skipped: '${GENTEST_OUTPUT}'")
    else()
        set(_gentest_output_path "${GENTEST_OUTPUT}")
        cmake_path(ABSOLUTE_PATH _gentest_output_path BASE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" NORMALIZE
                   OUTPUT_VARIABLE _gentest_output_abs)

        # Use a normalized key for case-insensitive filesystems.
        set(_gentest_output_key "${_gentest_output_abs}")
        if(WIN32)
            string(TOLOWER "${_gentest_output_key}" _gentest_output_key)
        endif()
        string(MD5 _gentest_output_md5 "${_gentest_output_key}")

        get_property(_gentest_prev_owner GLOBAL PROPERTY "GENTEST_CODEGEN_OUTPUT_OWNER_${_gentest_output_md5}")
        if(_gentest_prev_owner)
            if(NOT _gentest_prev_owner STREQUAL "${target}")
                message(FATAL_ERROR
                    "gentest_attach_codegen(${target}): OUTPUT '${_gentest_output_abs}' is already used by '${_gentest_prev_owner}'. "
                    "Each target must have a unique OUTPUT to avoid generated file clobbering.")
            endif()
            message(FATAL_ERROR
                "gentest_attach_codegen(${target}): OUTPUT '${_gentest_output_abs}' is registered multiple times for the same target. "
                "Call gentest_attach_codegen() once per target and list all SOURCES in that call.")
        endif()
        set_property(GLOBAL PROPERTY "GENTEST_CODEGEN_OUTPUT_OWNER_${_gentest_output_md5}" "${target}")

        # Also prevent the OUTPUT from overwriting any scanned source file.
        foreach(_gentest_src IN LISTS GENTEST_SOURCES)
            if("${_gentest_src}" MATCHES "\\$<")
                continue()
            endif()
            set(_gentest_src_path "${_gentest_src}")
            cmake_path(ABSOLUTE_PATH _gentest_src_path BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE
                       OUTPUT_VARIABLE _gentest_src_abs)

            set(_gentest_src_key "${_gentest_src_abs}")
            if(WIN32)
                string(TOLOWER "${_gentest_src_key}" _gentest_src_key)
            endif()
            if(_gentest_src_key STREQUAL _gentest_output_key)
                message(FATAL_ERROR
                    "gentest_attach_codegen(${target}): OUTPUT '${_gentest_output_abs}' would overwrite a scanned source file '${_gentest_src_abs}'.")
            endif()
        endforeach()
    endif()

    set(_gentest_codegen_target "")
    set(_gentest_codegen_executable "")
    if(GENTEST_CODEGEN_EXECUTABLE)
        set(_gentest_codegen_executable "${GENTEST_CODEGEN_EXECUTABLE}")
    elseif(GENTEST_CODEGEN_TARGET)
        if(NOT TARGET ${GENTEST_CODEGEN_TARGET})
            message(FATAL_ERROR "gentest_attach_codegen: GENTEST_CODEGEN_TARGET='${GENTEST_CODEGEN_TARGET}' does not exist")
        endif()
        set(_gentest_codegen_target "${GENTEST_CODEGEN_TARGET}")
        set(_gentest_codegen_executable $<TARGET_FILE:${GENTEST_CODEGEN_TARGET}>)
    elseif(TARGET gentest_codegen)
        set(_gentest_codegen_target gentest_codegen)
        set(_gentest_codegen_executable $<TARGET_FILE:gentest_codegen>)
    else()
        message(FATAL_ERROR
            "gentest_attach_codegen: no gentest code generator available. "
            "Either enable -DGENTEST_BUILD_CODEGEN=ON (native builds) or provide a host tool via "
            "-DGENTEST_CODEGEN_EXECUTABLE=<path> (cross builds).")
    endif()

    get_filename_component(_gentest_output_dir "${GENTEST_OUTPUT}" DIRECTORY)
    if(_gentest_output_dir STREQUAL "")
        set(_gentest_output_dir "${CMAKE_CURRENT_BINARY_DIR}")
    endif()

    string(MAKE_C_IDENTIFIER "${target}" _gentest_target_id)
    set(_gentest_mock_registry "${_gentest_output_dir}/${_gentest_target_id}_mock_registry.hpp")
    # Generate inline mock implementations as a header; it will be included
    # by the generated test implementation after including sources.
    set(_gentest_mock_impl "${_gentest_output_dir}/${_gentest_target_id}_mock_impl.hpp")

    set(_command_launcher ${_gentest_codegen_executable})
    if(GENTEST_USES_TERMINFO_SHIM AND UNIX AND NOT APPLE AND GENTEST_TERMINFO_SHIM_DIR)
        set(_gentest_ld_library_path "${GENTEST_TERMINFO_SHIM_DIR}")
        if(DEFINED ENV{LD_LIBRARY_PATH} AND NOT "$ENV{LD_LIBRARY_PATH}" STREQUAL "")
            string(APPEND _gentest_ld_library_path ":$ENV{LD_LIBRARY_PATH}")
        endif()
        set(_command_launcher ${CMAKE_COMMAND} -E env
            "LD_LIBRARY_PATH=${_gentest_ld_library_path}"
            ${_gentest_codegen_executable})
    endif()

    set(_command ${_command_launcher}
        --output ${GENTEST_OUTPUT}
        --entry ${GENTEST_ENTRY}
        --mock-registry ${_gentest_mock_registry}
        --mock-impl ${_gentest_mock_impl}
        --compdb ${CMAKE_BINARY_DIR})

    if(GENTEST_NO_INCLUDE_SOURCES)
        list(APPEND _command --no-include-sources)
    endif()
    if(GENTEST_STRICT_FIXTURE)
        list(APPEND _command --strict-fixture)
    endif()
    if(GENTEST_QUIET_CLANG)
        list(APPEND _command --quiet-clang)
    endif()

    list(APPEND _command ${GENTEST_SOURCES})

    list(APPEND _command --)
    list(APPEND _command -DGENTEST_CODEGEN=1)
    if(GENTEST_CLANG_ARGS)
        list(APPEND _command ${GENTEST_CLANG_ARGS})
    endif()

    # Add system include directories from CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES
    # to ensure gentest_codegen can parse headers correctly with all compilers
    set(_gentest_system_includes "${CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES}")
    if(_gentest_system_includes STREQUAL "" AND CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES)
        set(_gentest_system_includes "${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES}")
    endif()
    if(_gentest_system_includes)
        foreach(_inc_dir ${_gentest_system_includes})
            list(APPEND _command "-isystem" "${_inc_dir}")
        endforeach()
    endif()
    unset(_gentest_system_includes)

    set(_gentest_codegen_deps "")
    if(_gentest_codegen_target)
        list(APPEND _gentest_codegen_deps ${_gentest_codegen_target})
    endif()

    cmake_policy(PUSH)
    if(POLICY CMP0171)
        cmake_policy(SET CMP0171 NEW)
    endif()

    set(_gentest_custom_command_args
        OUTPUT ${GENTEST_OUTPUT} ${_gentest_mock_registry} ${_gentest_mock_impl}
        COMMAND ${_command}
        COMMAND_EXPAND_LISTS
        DEPENDS ${_gentest_codegen_deps} ${GENTEST_SOURCES} ${GENTEST_DEPENDS}
        COMMENT "Generating ${GENTEST_OUTPUT} for target ${target}"
        VERBATIM)
    if(POLICY CMP0171)
        list(APPEND _gentest_custom_command_args CODEGEN)
    endif()
    add_custom_command(${_gentest_custom_command_args})
    unset(_gentest_custom_command_args)

    cmake_policy(POP)

    # Only compile the generated test implementation; the mock impl header
    # is included by it and must not be compiled as a separate TU.
    target_sources(${target} PRIVATE ${GENTEST_OUTPUT})

    get_filename_component(_gentest_mock_dir "${_gentest_mock_registry}" DIRECTORY)
    target_include_directories(${target} PRIVATE ${_gentest_mock_dir})

    get_filename_component(_gentest_mock_header_name "${_gentest_mock_registry}" NAME)
    get_filename_component(_gentest_mock_impl_name "${_gentest_mock_impl}" NAME)
    target_compile_definitions(${target} PRIVATE
        GENTEST_MOCK_REGISTRY_PATH=${_gentest_mock_header_name}
        GENTEST_MOCK_IMPL_PATH=${_gentest_mock_impl_name}
    )
    if(GENTEST_USE_BOOST_JSON)
        target_compile_definitions(${target} PRIVATE GENTEST_USE_BOOST_JSON)
    endif()
    if(GENTEST_USE_BOOST_UUID)
        target_compile_definitions(${target} PRIVATE GENTEST_USE_BOOST_UUID)
    endif()
    if(_gentest_codegen_target)
        add_dependencies(${target} ${_gentest_codegen_target})
    endif()
endfunction()
