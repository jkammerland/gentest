include_guard(GLOBAL)

if(NOT DEFINED GENTEST_CODEGEN_EXECUTABLE)
    set(GENTEST_CODEGEN_EXECUTABLE "" CACHE FILEPATH
        "Path to a host-built gentest_codegen executable used when the in-tree gentest_codegen target is unavailable (e.g. cross-compiling).")
endif()

if(NOT DEFINED GENTEST_CODEGEN_TARGET)
    set(GENTEST_CODEGEN_TARGET "" CACHE STRING
        "CMake target name that produces a runnable gentest_codegen executable (alternative to GENTEST_CODEGEN_EXECUTABLE).")
endif()

if(NOT DEFINED GENTEST_CODEGEN_DEFAULT_CLANG_ARGS)
    set(GENTEST_CODEGEN_DEFAULT_CLANG_ARGS "-Wno-unknown-attributes;-Wno-attributes;-Wno-unknown-warning-option" CACHE STRING
        "Default extra clang arguments for gentest_codegen. Set empty to disable.")
endif()

function(_gentest_normalize_path_and_key input_path base_dir out_abs out_key)
    set(_gentest_path "${input_path}")
    cmake_path(ABSOLUTE_PATH _gentest_path BASE_DIRECTORY "${base_dir}" NORMALIZE OUTPUT_VARIABLE _gentest_abs)

    set(_gentest_key "${_gentest_abs}")
    if(WIN32)
        string(TOLOWER "${_gentest_key}" _gentest_key)
    endif()

    set(${out_abs} "${_gentest_abs}" PARENT_SCOPE)
    set(${out_key} "${_gentest_key}" PARENT_SCOPE)
endfunction()

function(_gentest_reserve_unique_owner property_prefix path_key owner out_prev_owner)
    string(MD5 _gentest_path_md5 "${path_key}")
    set(_gentest_owner_property "${property_prefix}_${_gentest_path_md5}")
    get_property(_gentest_prev_owner GLOBAL PROPERTY "${_gentest_owner_property}")
    if(NOT _gentest_prev_owner OR _gentest_prev_owner STREQUAL "${owner}")
        set_property(GLOBAL PROPERTY "${_gentest_owner_property}" "${owner}")
    endif()
    set(${out_prev_owner} "${_gentest_prev_owner}" PARENT_SCOPE)
endfunction()

function(gentest_attach_codegen target)
    set(options NO_INCLUDE_SOURCES STRICT_FIXTURE QUIET_CLANG)
    set(one_value_args OUTPUT OUTPUT_DIR ENTRY)
    set(multi_value_args SOURCES CLANG_ARGS DEPENDS)
    cmake_parse_arguments(GENTEST "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT GENTEST_ENTRY)
        set(GENTEST_ENTRY gentest::run_all_tests)
    endif()

    string(MAKE_C_IDENTIFIER "${target}" _gentest_target_id)

    # Scan sources: explicit SOURCES preferred, otherwise pull from target.
    set(_gentest_scan_sources "${GENTEST_SOURCES}")
    if(NOT _gentest_scan_sources)
        get_target_property(_gentest_scan_sources ${target} SOURCES)
        get_target_property(_gentest_module_sources ${target} CXX_MODULE_SET)
        if(_gentest_module_sources AND NOT _gentest_module_sources STREQUAL "NOTFOUND")
            list(APPEND _gentest_scan_sources ${_gentest_module_sources})
        endif()
        get_target_property(_gentest_module_sets ${target} CXX_MODULE_SETS)
        if(_gentest_module_sets AND NOT _gentest_module_sets STREQUAL "NOTFOUND")
            foreach(_gentest_module_set IN LISTS _gentest_module_sets)
                get_target_property(_gentest_named_module_sources ${target} "CXX_MODULE_SET_${_gentest_module_set}")
                if(_gentest_named_module_sources AND NOT _gentest_named_module_sources STREQUAL "NOTFOUND")
                    list(APPEND _gentest_scan_sources ${_gentest_named_module_sources})
                endif()
            endforeach()
            unset(_gentest_module_set)
            unset(_gentest_named_module_sources)
        endif()
    endif()
    if(NOT _gentest_scan_sources)
        message(FATAL_ERROR "gentest_attach_codegen(${target}): SOURCES not provided and target has no SOURCES property")
    endif()
    list(REMOVE_DUPLICATES _gentest_scan_sources)

    # Select translation units (C++/CUDA/module sources only, no generator expressions).
    set(_gentest_tus "")
    set(_gentest_tu_source_entries "")
    set(_gentest_tu_extensions "")
    set(_gentest_module_tus "")
    set(_gentest_skipped_genex_sources "")
    set(_gentest_scan_extensions
        .C
        .cc
        .cpp
        .cxx
        .c++
        .cp
        .cu
        .cppm
        .ccm
        .cxxm
        .c++m
        .ixx
        .mxx)
    set(_gentest_named_module_extensions
        .cppm
        .ccm
        .cxxm
        .c++m
        .ixx
        .mxx)
    foreach(_gentest_src IN LISTS _gentest_scan_sources)
        if("${_gentest_src}" MATCHES "\\$<")
            list(APPEND _gentest_skipped_genex_sources "${_gentest_src}")
            continue()
        endif()
        get_filename_component(_gentest_ext "${_gentest_src}" EXT)
        set(_gentest_ext_key "${_gentest_ext}")
        if(NOT _gentest_ext STREQUAL ".C")
            string(TOLOWER "${_gentest_ext}" _gentest_ext_key)
        endif()
        if(NOT _gentest_ext_key IN_LIST _gentest_scan_extensions)
            continue()
        endif()
        _gentest_normalize_path_and_key("${_gentest_src}" "${CMAKE_CURRENT_SOURCE_DIR}" _gentest_src_abs _gentest_src_key)
        list(APPEND _gentest_tu_source_entries "${_gentest_src}")
        list(APPEND _gentest_tus "${_gentest_src_abs}")
        list(APPEND _gentest_tu_extensions "${_gentest_ext}")

        set(_gentest_is_named_module FALSE)
        if(_gentest_ext_key IN_LIST _gentest_named_module_extensions)
            set(_gentest_is_named_module TRUE)
        elseif(EXISTS "${_gentest_src_abs}")
            file(READ "${_gentest_src_abs}" _gentest_source_text)
            if(_gentest_source_text MATCHES "(^|[\n\r])[ \t]*export[ \t]+module[ \t]+[^;\n\r]+;"
                OR _gentest_source_text MATCHES "(^|[\n\r])[ \t]*module[ \t]+[A-Za-z_][A-Za-z0-9_:.]*[ \t]*;")
                set(_gentest_is_named_module TRUE)
            endif()
            unset(_gentest_source_text)
        endif()
        if(_gentest_is_named_module)
            list(APPEND _gentest_module_tus "${_gentest_src_abs}")
        endif()
        unset(_gentest_is_named_module)
    endforeach()

    if(_gentest_skipped_genex_sources)
        list(LENGTH _gentest_skipped_genex_sources _gentest_skipped_genex_count)
        message(WARNING
            "gentest_attach_codegen(${target}): skipping ${_gentest_skipped_genex_count} generator-expression SOURCES entries. "
            "Pass concrete files via SOURCES=... if you need those scanned/wrapped.")
    endif()

    if(NOT _gentest_tus)
        message(FATAL_ERROR "gentest_attach_codegen(${target}): no C++ translation units found to scan")
    endif()

    # Mode selection:
    # - If OUTPUT is provided, emit a single manifest TU (legacy).
    # - Otherwise, emit a wrapper TU + header per translation unit and replace
    #   the target sources (gtest/catch/doctest-like workflow).
    set(_gentest_mode "tu")
    if(GENTEST_OUTPUT)
        set(_gentest_mode "manifest")
    endif()

    if(_gentest_mode STREQUAL "tu" AND CMAKE_CONFIGURATION_TYPES)
        message(FATAL_ERROR
            "gentest_attach_codegen(${target}): TU wrapper mode is not supported with multi-config generators. "
            "Use a single-config generator (separate build dirs) or pass OUTPUT=... to use manifest mode.")
    endif()
    if(_gentest_mode STREQUAL "tu" AND _gentest_module_tus)
        string(JOIN ", " _gentest_module_inputs ${_gentest_module_tus})
        message(FATAL_ERROR
            "gentest_attach_codegen(${target}): TU wrapper mode does not support named module interface sources "
            "(${_gentest_module_inputs}). Use OUTPUT=... (manifest mode) for targets that include module interface units.")
    endif()

    if(_gentest_mode STREQUAL "manifest")
        if(NOT GENTEST_OUTPUT)
            set(GENTEST_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${target}_generated.cpp")
        endif()

        # Configure-time collision checks: prevent multiple targets (or multiple
        # calls) from writing to the same generated OUTPUT file.
        if("${GENTEST_OUTPUT}" MATCHES "\\$<")
            message(WARNING "gentest_attach_codegen(${target}): OUTPUT contains generator expressions; collision checks skipped: '${GENTEST_OUTPUT}'")
        else()
            _gentest_normalize_path_and_key("${GENTEST_OUTPUT}" "${CMAKE_CURRENT_BINARY_DIR}" _gentest_output_abs _gentest_output_key)
            _gentest_reserve_unique_owner("GENTEST_CODEGEN_OUTPUT_OWNER" "${_gentest_output_key}" "${target}" _gentest_prev_owner)
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

            # Also prevent the OUTPUT from overwriting any scanned source file.
            foreach(_gentest_src IN LISTS _gentest_tus)
                _gentest_normalize_path_and_key("${_gentest_src}" "${CMAKE_CURRENT_SOURCE_DIR}" _gentest_src_abs _gentest_src_key)
                if(_gentest_src_key STREQUAL _gentest_output_key)
                    message(FATAL_ERROR
                        "gentest_attach_codegen(${target}): OUTPUT '${_gentest_output_abs}' would overwrite a scanned source file '${_gentest_src_abs}'.")
                endif()
            endforeach()
        endif()

        get_filename_component(_gentest_output_dir "${GENTEST_OUTPUT}" DIRECTORY)
        if(_gentest_output_dir STREQUAL "")
            set(_gentest_output_dir "${CMAKE_CURRENT_BINARY_DIR}")
        endif()
    else()
        if(GENTEST_NO_INCLUDE_SOURCES)
            message(FATAL_ERROR
                "gentest_attach_codegen(${target}): NO_INCLUDE_SOURCES is not supported in TU wrapper mode, "
                "because wrappers must include the original translation unit. "
                "Use OUTPUT=... to switch to legacy manifest mode if you need NO_INCLUDE_SOURCES.")
        endif()

        if(GENTEST_OUTPUT_DIR)
            set(_gentest_output_dir "${GENTEST_OUTPUT_DIR}")
        else()
            set(_gentest_output_dir "${CMAKE_CURRENT_BINARY_DIR}/gentest/${_gentest_target_id}")
        endif()

        if("${_gentest_output_dir}" MATCHES "\\$<")
            message(FATAL_ERROR
                "gentest_attach_codegen(${target}): OUTPUT_DIR contains generator expressions, which is not supported in TU wrapper mode "
                "(requires a concrete directory to generate shim translation units). "
                "Pass a concrete OUTPUT_DIR, or use OUTPUT=... to switch to manifest mode.")
        endif()

        # Normalize OUTPUT_DIR to an absolute path so wrapper file paths match
        # compile_commands.json entries (avoids falling back to synthetic tool
        # invocations).
        _gentest_normalize_path_and_key("${_gentest_output_dir}" "${CMAKE_CURRENT_BINARY_DIR}" _gentest_outdir_abs _gentest_outdir_key)
        set(_gentest_output_dir "${_gentest_outdir_abs}")

        # Configure-time collision checks for the output directory (avoid
        # clobbering among targets).
        _gentest_reserve_unique_owner("GENTEST_CODEGEN_OUTDIR_OWNER" "${_gentest_outdir_key}" "${target}" _gentest_prev_owner)
        if(_gentest_prev_owner AND NOT _gentest_prev_owner STREQUAL "${target}")
            message(FATAL_ERROR
                "gentest_attach_codegen(${target}): OUTPUT_DIR '${_gentest_outdir_abs}' is already used by '${_gentest_prev_owner}'. "
                "Each target should have a unique OUTPUT_DIR to avoid generated file clobbering.")
        endif()
    endif()

    set(_gentest_codegen_target "")
    set(_gentest_codegen_executable "")
    if(CMAKE_CROSSCOMPILING AND NOT GENTEST_CODEGEN_EXECUTABLE AND NOT GENTEST_CODEGEN_TARGET)
        message(FATAL_ERROR
            "gentest_attach_codegen(${target}): cross-compiling requires a host gentest_codegen. "
            "Set -DGENTEST_CODEGEN_EXECUTABLE=<path> or -DGENTEST_CODEGEN_TARGET=<target>.")
    endif()
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

    set(_gentest_mock_registry "${_gentest_output_dir}/${_gentest_target_id}_mock_registry.hpp")
    # Generate inline mock implementations as a header; it will be included by
    # the generated wrapper translation units after including sources.
    set(_gentest_mock_impl "${_gentest_output_dir}/${_gentest_target_id}_mock_impl.hpp")

    # Compute per-TU wrapper outputs (TU mode).
    set(_gentest_wrapper_cpp "")
    set(_gentest_wrapper_headers "")
    if(_gentest_mode STREQUAL "tu")
        set(_gentest_idx 0)
        foreach(_tu IN LISTS _gentest_tus)
            list(GET _gentest_tu_extensions ${_gentest_idx} _tu_ext)
            get_filename_component(_stem "${_tu}" NAME_WE)
            string(REGEX REPLACE "[^A-Za-z0-9_]" "_" _stem "${_stem}")
            if(_stem STREQUAL "")
                set(_stem "tu")
            endif()
            set(_idx_str "${_gentest_idx}")
            string(LENGTH "${_idx_str}" _idx_len)
            if(_idx_len LESS 4)
                math(EXPR _pad "4 - ${_idx_len}")
                string(REPEAT "0" ${_pad} _zeros)
                set(_idx_str "${_zeros}${_idx_str}")
            endif()
            list(APPEND _gentest_wrapper_headers "${_gentest_output_dir}/tu_${_idx_str}_${_stem}.gentest.h")
            list(APPEND _gentest_wrapper_cpp "${_gentest_output_dir}/tu_${_idx_str}_${_stem}.gentest${_tu_ext}")
            math(EXPR _gentest_idx "${_gentest_idx} + 1")
        endforeach()
        unset(_gentest_idx)
    endif()

    if(_gentest_mode STREQUAL "tu")
        # TU wrapper mode uses configure-time generated shim translation units
        # (*.gentest.<original-ext>) that include the original source.
        # gentest_codegen
        # emits only the corresponding registration headers (*.gentest.h).
        if(NOT "${_gentest_output_dir}" MATCHES "\\$<")
            file(MAKE_DIRECTORY "${_gentest_output_dir}")
        endif()

        list(LENGTH _gentest_wrapper_cpp _gentest_wrapper_count)
        math(EXPR _gentest_last "${_gentest_wrapper_count} - 1")
        foreach(_idx RANGE 0 ${_gentest_last})
            list(GET _gentest_tus ${_idx} _orig_abs)
            list(GET _gentest_wrapper_cpp ${_idx} _wrap_cpp)
            list(GET _gentest_wrapper_headers ${_idx} _wrap_header)
            get_filename_component(_wrap_header_name "${_wrap_header}" NAME)

            # Keep includes relative to the wrapper directory to avoid embedding
            # absolute paths in generated sources.
            file(RELATIVE_PATH _rel_src "${_gentest_output_dir}" "${_orig_abs}")
            string(REPLACE "\\" "/" _rel_src "${_rel_src}")

            set(_shim_content
"// This file is auto-generated by gentest (CMake shim).\n\
// Do not edit manually.\n\
\n\
// Include the original translation unit so fixture types and test bodies are\n\
// visible for wrappers.\n\
#include \"${_rel_src}\"\n\
\n\
// Include generated registrations after the original TU is visible.\n\
// During codegen runs, this header may not exist yet.\n\
#ifndef GENTEST_CODEGEN\n\
#include \"${_wrap_header_name}\"\n\
#endif\n")

            file(GENERATE OUTPUT "${_wrap_cpp}" CONTENT "${_shim_content}")
        endforeach()

        unset(_gentest_wrapper_count)
        unset(_gentest_last)
        unset(_wrap_header_name)
        unset(_rel_src)
        unset(_shim_content)

        set_source_files_properties(${_gentest_wrapper_cpp} PROPERTIES GENERATED TRUE SKIP_UNITY_BUILD_INCLUSION ON)
        set_source_files_properties(${_gentest_wrapper_headers} PROPERTIES GENERATED TRUE)

        # Preserve per-source compile properties when replacing the original TU
        # with a generated wrapper TU (which includes the original source).
        set(_gentest_source_props
            COMPILE_DEFINITIONS
            COMPILE_OPTIONS
            INCLUDE_DIRECTORIES
            COMPILE_FLAGS
            LANGUAGE
            CUDA_ARCHITECTURES
            CUDA_STANDARD
            CUDA_STANDARD_REQUIRED
            CUDA_EXTENSIONS
            CUDA_RUNTIME_LIBRARY
            CUDA_SEPARABLE_COMPILATION
            CUDA_PTX_COMPILATION
            CUDA_RESOLVE_DEVICE_SYMBOLS)
        set(_gentest_configs "")
        if(CMAKE_CONFIGURATION_TYPES)
            set(_gentest_configs ${CMAKE_CONFIGURATION_TYPES})
        elseif(CMAKE_BUILD_TYPE)
            set(_gentest_configs ${CMAKE_BUILD_TYPE})
        endif()
        foreach(_cfg IN LISTS _gentest_configs)
            string(TOUPPER "${_cfg}" _cfg_upper)
            foreach(_base IN ITEMS COMPILE_DEFINITIONS COMPILE_OPTIONS INCLUDE_DIRECTORIES COMPILE_FLAGS)
                list(APPEND _gentest_source_props "${_base}_${_cfg_upper}")
            endforeach()
        endforeach()
        list(REMOVE_DUPLICATES _gentest_source_props)

        list(LENGTH _gentest_wrapper_cpp _gentest_wrapper_count)
        math(EXPR _gentest_last "${_gentest_wrapper_count} - 1")
        foreach(_idx RANGE 0 ${_gentest_last})
            list(GET _gentest_tu_source_entries ${_idx} _orig_entry)
            list(GET _gentest_tus ${_idx} _orig_abs)
            list(GET _gentest_wrapper_cpp ${_idx} _wrap_cpp)

            foreach(_prop IN LISTS _gentest_source_props)
                get_source_file_property(_val "${_orig_entry}" ${_prop})
                if(_val STREQUAL "NOTFOUND")
                    get_source_file_property(_val "${_orig_abs}" ${_prop})
                endif()
                if(NOT _val STREQUAL "NOTFOUND")
                    set_source_files_properties("${_wrap_cpp}" PROPERTIES ${_prop} "${_val}")
                endif()
            endforeach()
        endforeach()

        unset(_gentest_source_props)
        unset(_gentest_configs)
        unset(_gentest_wrapper_count)
        unset(_gentest_last)
    endif()

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
        --mock-registry ${_gentest_mock_registry}
        --mock-impl ${_gentest_mock_impl}
        --compdb ${CMAKE_BINARY_DIR}
        --source-root ${CMAKE_SOURCE_DIR})

    if(_gentest_mode STREQUAL "manifest")
        list(APPEND _command --output ${GENTEST_OUTPUT})
        list(APPEND _command --entry ${GENTEST_ENTRY})
        if(GENTEST_NO_INCLUDE_SOURCES)
            list(APPEND _command --no-include-sources)
        endif()
    else()
        list(APPEND _command --tu-out-dir ${_gentest_output_dir})
    endif()

    if(GENTEST_STRICT_FIXTURE)
        list(APPEND _command --strict-fixture)
    endif()
    if(GENTEST_QUIET_CLANG)
        list(APPEND _command --quiet-clang)
    endif()

    if(_gentest_mode STREQUAL "tu")
        # In TU mode we scan the shim wrapper sources, which are present in the
        # compilation database. They include the original sources.
        list(APPEND _command ${_gentest_wrapper_cpp})
    else()
        list(APPEND _command ${_gentest_tus})
    endif()

    list(APPEND _command --)
    list(APPEND _command -DGENTEST_CODEGEN=1)
    if(CMAKE_CROSSCOMPILING)
        if(CMAKE_CXX_COMPILER_TARGET)
            list(APPEND _command "--target=${CMAKE_CXX_COMPILER_TARGET}")
        elseif(CMAKE_C_COMPILER_TARGET)
            list(APPEND _command "--target=${CMAKE_C_COMPILER_TARGET}")
        endif()
    endif()
    if(GENTEST_CODEGEN_DEFAULT_CLANG_ARGS AND NOT GENTEST_CODEGEN_DEFAULT_CLANG_ARGS STREQUAL "")
        list(APPEND _command ${GENTEST_CODEGEN_DEFAULT_CLANG_ARGS})
    endif()
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

    if(_gentest_mode STREQUAL "manifest")
        set(_gentest_codegen_outputs ${GENTEST_OUTPUT} ${_gentest_mock_registry} ${_gentest_mock_impl})
    else()
        set(_gentest_codegen_outputs ${_gentest_wrapper_headers} ${_gentest_mock_registry} ${_gentest_mock_impl})
    endif()

    set(_gentest_custom_command_args
        OUTPUT ${_gentest_codegen_outputs}
        COMMAND ${_command}
        COMMAND_EXPAND_LISTS
        DEPENDS ${_gentest_codegen_deps} ${_gentest_tus} ${GENTEST_DEPENDS}
        COMMENT "Running gentest_codegen for target ${target}"
        VERBATIM)
    if(POLICY CMP0171)
        list(APPEND _gentest_custom_command_args CODEGEN)
    endif()
    add_custom_command(${_gentest_custom_command_args})
    unset(_gentest_custom_command_args)

    cmake_policy(POP)

    if(_gentest_mode STREQUAL "manifest")
        target_sources(${target} PRIVATE ${GENTEST_OUTPUT})
    else()
        # Replace the original translation units with generated wrappers to
        # avoid ODR violations (wrapper TU includes the original .cpp).
        get_target_property(_gentest_target_sources ${target} SOURCES)
        if(NOT _gentest_target_sources)
            set(_gentest_target_sources "")
        endif()

        set(_gentest_wrap_keys "")
        foreach(_tu IN LISTS _gentest_tus)
            _gentest_normalize_path_and_key("${_tu}" "${CMAKE_CURRENT_SOURCE_DIR}" _abs _key)
            list(APPEND _gentest_wrap_keys "${_key}")
        endforeach()

        set(_gentest_new_sources "")
        foreach(_src IN LISTS _gentest_target_sources)
            if("${_src}" MATCHES "\\$<")
                list(APPEND _gentest_new_sources "${_src}")
                continue()
            endif()
            list(FIND _gentest_tu_source_entries "${_src}" _src_entry_idx)
            if(NOT _src_entry_idx EQUAL -1)
                continue()
            endif()
            _gentest_normalize_path_and_key("${_src}" "${CMAKE_CURRENT_SOURCE_DIR}" _abs _key)
            list(FIND _gentest_wrap_keys "${_key}" _found)
            if(_found EQUAL -1)
                list(APPEND _gentest_new_sources "${_src}")
            endif()
        endforeach()

        list(APPEND _gentest_new_sources ${_gentest_wrapper_cpp})
        set_property(TARGET ${target} PROPERTY SOURCES "${_gentest_new_sources}")

        # Ensure code generation happens before compiling wrapper shims that
        # include the generated headers.
        add_custom_target(gentest_codegen_${_gentest_target_id} DEPENDS ${_gentest_codegen_outputs})
        if(TARGET gentest_codegen_all)
            add_dependencies(gentest_codegen_all gentest_codegen_${_gentest_target_id})
        endif()
        add_dependencies(${target} gentest_codegen_${_gentest_target_id})
    endif()

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

function(_gentest_write_discover_tests_script out_script)
    set(_gentest_script_dir "${CMAKE_BINARY_DIR}/gentest")
    file(MAKE_DIRECTORY "${_gentest_script_dir}")

    set(_gentest_add_tests_script "${_gentest_script_dir}/GentestAddTests.cmake")
    set(_gentest_script_content [====[
cmake_minimum_required(VERSION 3.31)

set(_gentest_cmake_command "@CMAKE_COMMAND@")

function(_gentest_add_command name test_name)
  set(args "")
  foreach(arg ${ARGN})
    if(arg MATCHES "[^-./:a-zA-Z0-9_]")
      string(APPEND args " [==[${arg}]==]")
    else()
      string(APPEND args " ${arg}")
    endif()
  endforeach()
  string(APPEND script "${name}(${test_name} ${args})\n")
  set(script "${script}" PARENT_SCOPE)
endfunction()

function(_gentest_generate_testname_guards output open_guard_var close_guard_var)
  set(open_guard "[=[")
  set(close_guard "]=]")
  set(counter 1)
  while("${output}" MATCHES "${close_guard}")
    math(EXPR counter "${counter} + 1")
    string(REPEAT "=" ${counter} equals)
    set(open_guard "[${equals}[")
    set(close_guard "]${equals}]")
  endwhile()
  set(${open_guard_var} "${open_guard}" PARENT_SCOPE)
  set(${close_guard_var} "${close_guard}" PARENT_SCOPE)
endfunction()

function(_gentest_escape_square_brackets output bracket placeholder placeholder_var output_var)
  if("${output}" MATCHES "\\${bracket}")
    set(placeholder "${placeholder}")
    while("${output}" MATCHES "${placeholder}")
      set(placeholder "${placeholder}_")
    endwhile()
    string(REPLACE "${bracket}" "${placeholder}" output "${output}")
    set(${placeholder_var} "${placeholder}" PARENT_SCOPE)
    set(${output_var} "${output}" PARENT_SCOPE)
  endif()
endfunction()

function(_gentest_wildcard_to_regex out_var pat)
  # Convert a simple wildcard (*, ?) pattern to an anchored CMake regex.
  set(_s "${pat}")
  string(REPLACE "\\" "\\\\" _s "${_s}")
  string(REPLACE "." "\\." _s "${_s}")
  string(REPLACE "+" "\\+" _s "${_s}")
  string(REPLACE "(" "\\(" _s "${_s}")
  string(REPLACE ")" "\\)" _s "${_s}")
  string(REPLACE "[" "\\[" _s "${_s}")
  string(REPLACE "]" "\\]" _s "${_s}")
  string(REPLACE "{" "\\{" _s "${_s}")
  string(REPLACE "}" "\\}" _s "${_s}")
  string(REPLACE "^" "\\^" _s "${_s}")
  string(REPLACE "$" "\\$" _s "${_s}")
  string(REPLACE "|" "\\|" _s "${_s}")
  string(REPLACE "*" ".*" _s "${_s}")
  string(REPLACE "?" "." _s "${_s}")
  set(${out_var} "^${_s}$" PARENT_SCOPE)
endfunction()

function(_gentest_ensure_check_death_script out_var)
  set(_script_dir "${CMAKE_CURRENT_LIST_DIR}")
  file(MAKE_DIRECTORY "${_script_dir}")
  set(_script "${_script_dir}/GentestCheckDeath.cmake")
  file(WRITE "${_script}" [==[
# Requires:
#  -DPROG=<path to test binary>
#  -DARGS=<optional CLI args>
#  -DENV_VARS=<optional env vars (list of KEY=VALUE)>
#  -DEXPECT_SUBSTRING=<substring expected in combined output>

if(NOT DEFINED PROG)
  message(FATAL_ERROR "CheckDeath.cmake: PROG not set")
endif()

set(_emu)
if(DEFINED EMU)
  if(EMU MATCHES ";")
    set(_emu ${EMU}) # already a list
  else()
    separate_arguments(_emu NATIVE_COMMAND "${EMU}") # string
  endif()
endif()

set(_args)
if(DEFINED ARGS)
  if(ARGS MATCHES ";")
    set(_args ${ARGS})
  else()
    separate_arguments(_args NATIVE_COMMAND "${ARGS}")
  endif()
endif()

set(_command ${_emu} "${PROG}" ${_args})
if(DEFINED ENV_VARS)
  set(_env)
  foreach(kv IN LISTS ENV_VARS)
    list(APPEND _env "${kv}")
  endforeach()
  set(_command ${CMAKE_COMMAND} -E env ${_env} ${_emu} "${PROG}" ${_args})
endif()

execute_process(
  COMMAND ${_command}
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

set(_all "${_out}\n${_err}")

set(_missing_case "")
foreach(_arg IN LISTS _args)
  if(_arg MATCHES "^--run=(.+)$")
    set(_missing_case "${CMAKE_MATCH_1}")
    break()
  endif()
endforeach()

set(_missing_case_line FALSE)
if(NOT _missing_case STREQUAL "")
  string(REPLACE "\r\n" "\n" _all_norm "${_all}")
  string(REPLACE "\n" ";" _all_lines "${_all_norm}")
  foreach(_line IN LISTS _all_lines)
    string(STRIP "${_line}" _line_trim)
    if(_line_trim STREQUAL "Case not found: ${_missing_case}" OR _line_trim STREQUAL "Test not found: ${_missing_case}")
      set(_missing_case_line TRUE)
      break()
    endif()
  endforeach()
endif()

if(_missing_case_line AND _rc EQUAL 3)
  message(STATUS "[ SKIP ] Death test not present in this build configuration")
  return()
endif()

# Compatibility fallback for older runners that emit the "not found" line
# but still use a generic non-zero exit code.
if(_missing_case_line)
  message(STATUS "[ SKIP ] Death test not present in this build configuration")
  return()
endif()

if(_all MATCHES "(^|\n)\\[ SKIP \\]")
  message(STATUS "[ SKIP ] Death test skipped by test binary")
  return()
endif()

if(_all MATCHES "tagged as a death test" OR _all MATCHES "death tests excluded")
  message(FATAL_ERROR "Death test did not run with --include-death. Output:\n${_all}")
endif()

if(_rc EQUAL 0)
  message(FATAL_ERROR "Expected process to abort/exit non-zero, but exit code was 0. Output:\n${_all}")
endif()

if(_all MATCHES "(^|\n)\\[ FAIL \\]")
  message(FATAL_ERROR "Death test exited non-zero but reported a normal test failure. Output:\n${_all}")
endif()

if(DEFINED EXPECT_SUBSTRING)
  string(FIND "${_all}" "${EXPECT_SUBSTRING}" _pos)
  if(_pos EQUAL -1)
    message(FATAL_ERROR "Expected substring not found in output: '${EXPECT_SUBSTRING}'. Output:\n${_all}")
  endif()
endif()

message(STATUS "Death test passed (non-zero exit and expected output present)")
]==])
  set(${out_var} "${_script}" PARENT_SCOPE)
endfunction()

function(gentest_discover_tests_impl)
  set(options "")
  set(oneValueArgs
    TEST_EXECUTABLE
    TEST_WORKING_DIR
    TEST_PREFIX
    TEST_SUFFIX
    TEST_FILTER
    TEST_LIST
    CTEST_FILE
    TEST_DISCOVERY_TIMEOUT
    # The following are all multi-value arguments in gentest_discover_tests(),
    # but are each passed as a single argument to preserve escaping.
    TEST_EXTRA_ARGS
    TEST_DISCOVERY_EXTRA_ARGS
    TEST_PROPERTIES
    TEST_EXECUTOR
    EXPECT_SUBSTRING
  )
  set(multiValueArgs "")
  cmake_parse_arguments(PARSE_ARGV 0 arg "${options}" "${oneValueArgs}" "${multiValueArgs}")

  set(prefix "${arg_TEST_PREFIX}")
  set(suffix "${arg_TEST_SUFFIX}")
  set(death_prefix "death/")
  set(death_suffix "")
  set(script)
  set(tests)
  set(file_write_mode WRITE)

  if(NOT EXISTS "${arg_TEST_EXECUTABLE}")
    message(FATAL_ERROR "Specified test executable does not exist: '${arg_TEST_EXECUTABLE}'")
  endif()

  set(launcher_args "")
  if(NOT "${arg_TEST_EXECUTOR}" STREQUAL "")
    list(JOIN arg_TEST_EXECUTOR "]==] [==[" launcher_args)
    set(launcher_args "[==[${launcher_args}]==]")
  endif()

  set(discovery_extra_args "")
  if(NOT "${arg_TEST_DISCOVERY_EXTRA_ARGS}" STREQUAL "")
    list(JOIN arg_TEST_DISCOVERY_EXTRA_ARGS "]==] [==[" discovery_extra_args)
    set(discovery_extra_args "[==[${discovery_extra_args}]==]")
  endif()

  if("${arg_TEST_DISCOVERY_TIMEOUT}" STREQUAL "")
    set(arg_TEST_DISCOVERY_TIMEOUT 5)
  endif()

  cmake_language(EVAL CODE
    "execute_process(
      COMMAND ${launcher_args} [==[${arg_TEST_EXECUTABLE}]==] --list-tests ${discovery_extra_args}
      WORKING_DIRECTORY [==[${arg_TEST_WORKING_DIR}]==]
      TIMEOUT ${arg_TEST_DISCOVERY_TIMEOUT}
      OUTPUT_VARIABLE output
      ERROR_VARIABLE error_output
      RESULT_VARIABLE result
    )"
  )
  if(NOT result EQUAL 0)
    string(REPLACE "\n" "\n    " output "${output}")
    string(REPLACE "\n" "\n    " error_output "${error_output}")
    if(arg_TEST_EXECUTOR)
      set(path "${arg_TEST_EXECUTOR} ${arg_TEST_EXECUTABLE}")
    else()
      set(path "${arg_TEST_EXECUTABLE}")
    endif()
    message(FATAL_ERROR
      "Error running test executable.\n"
      "  Path: '${path}'\n"
      "  Working directory: '${arg_TEST_WORKING_DIR}'\n"
      "  Result: ${result}\n"
      "  Stdout:\n"
      "    ${output}\n"
      "  Stderr:\n"
      "    ${error_output}\n"
    )
  endif()

  cmake_language(EVAL CODE
    "execute_process(
      COMMAND ${launcher_args} [==[${arg_TEST_EXECUTABLE}]==] --list-death ${discovery_extra_args}
      WORKING_DIRECTORY [==[${arg_TEST_WORKING_DIR}]==]
      TIMEOUT ${arg_TEST_DISCOVERY_TIMEOUT}
      OUTPUT_VARIABLE death_output
      ERROR_VARIABLE death_error_output
      RESULT_VARIABLE death_result
    )"
  )
  if(NOT death_result EQUAL 0)
    string(REPLACE "\n" "\n    " death_output "${death_output}")
    string(REPLACE "\n" "\n    " death_error_output "${death_error_output}")
    if(arg_TEST_EXECUTOR)
      set(path "${arg_TEST_EXECUTOR} ${arg_TEST_EXECUTABLE}")
    else()
      set(path "${arg_TEST_EXECUTABLE}")
    endif()
    message(FATAL_ERROR
      "Error running test executable with --list-death.\n"
      "  Path: '${path}'\n"
      "  Working directory: '${arg_TEST_WORKING_DIR}'\n"
      "  Result: ${death_result}\n"
      "  Stdout:\n"
      "    ${death_output}\n"
      "  Stderr:\n"
      "    ${death_error_output}\n"
      "  Hint: update the test binary to a gentest version that supports --list-death.\n"
    )
  endif()

  set(filter_regex "")
  if(arg_TEST_FILTER)
    _gentest_wildcard_to_regex(filter_regex "${arg_TEST_FILTER}")
  endif()

  set(_combined_output "${output}
${death_output}")
  _gentest_generate_testname_guards("${_combined_output}" open_guard close_guard)

  function(_gentest_parse_list raw_output out_var)
    set(_out "${raw_output}")
    _gentest_escape_square_brackets("${_out}" "[" "__osb" open_sb _out)
    _gentest_escape_square_brackets("${_out}" "]" "__csb" close_sb _out)
    string(REPLACE [[;]] [[\;]] _out "${_out}")
    string(REPLACE "\r\n" "\n" _out "${_out}")
    string(REPLACE "\n" ";" _out "${_out}")

    set(_cases "")
    foreach(line IN LISTS _out)
      string(STRIP "${line}" case_name_raw)
      if(case_name_raw STREQUAL "")
        continue()
      endif()

      set(case_line "${case_name_raw}")
      if(open_sb)
        string(REPLACE "${open_sb}" "[" case_line "${case_line}")
      endif()
      if(close_sb)
        string(REPLACE "${close_sb}" "]" case_line "${case_line}")
      endif()
      # Restore escaped semicolons now that we're processing a single line.
      string(REPLACE "\\;" ";" case_line "${case_line}")

      list(APPEND _cases "${case_line}")
    endforeach()
    set(${out_var} "${_cases}" PARENT_SCOPE)
  endfunction()

  _gentest_parse_list("${output}" normal_cases)
  _gentest_parse_list("${death_output}" death_cases)

  if(death_cases)
    _gentest_ensure_check_death_script(_gentest_check_death_script)
  endif()

  foreach(case_id IN LISTS normal_cases)
    if(filter_regex AND NOT case_id MATCHES "${filter_regex}")
      continue()
    endif()
    list(FIND death_cases "${case_id}" _death_idx)
    if(_death_idx GREATER_EQUAL 0)
      continue()
    endif()

    set(testname "${prefix}${case_id}${suffix}")
    set(guarded_testname "${open_guard}${testname}${close_guard}")

    # Preserve empty arguments in TEST_EXECUTOR and EXTRA_ARGS by forwarding them as a bracket-quoted list.
    string(APPEND script "add_test(${guarded_testname} ${launcher_args}")
    foreach(arg IN ITEMS
      "${arg_TEST_EXECUTABLE}"
      "--run=${case_id}"
      )
      if(arg MATCHES "[^-./:a-zA-Z0-9_]")
        string(APPEND script " [==[${arg}]==]")
      else()
        string(APPEND script " ${arg}")
      endif()
    endforeach()
    if(arg_TEST_EXTRA_ARGS)
      list(JOIN arg_TEST_EXTRA_ARGS "]==] [==[" extra_args)
      string(APPEND script " [==[${extra_args}]==]")
    endif()
    string(APPEND script ")\n")

    _gentest_add_command(set_tests_properties
      "${guarded_testname}"
      PROPERTIES
      WORKING_DIRECTORY "${arg_TEST_WORKING_DIR}"
      ${arg_TEST_PROPERTIES}
    )

    string(REPLACE [[;]] [[\;]] _testname_escaped "${testname}")
    list(APPEND tests "${_testname_escaped}")

    string(LENGTH "${script}" script_len)
    if(script_len GREATER "50000")
      file(${file_write_mode} "${arg_CTEST_FILE}" "${script}")
      set(file_write_mode APPEND)
      set(script "")
    endif()
  endforeach()

  foreach(case_id IN LISTS death_cases)
    if(filter_regex AND NOT case_id MATCHES "${filter_regex}")
      continue()
    endif()

    set(testname "${prefix}${death_prefix}${case_id}${death_suffix}${suffix}")
    set(guarded_testname "${open_guard}${testname}${close_guard}")

    set(death_args_list "--include-death" "--run=${case_id}")
    if(arg_TEST_EXTRA_ARGS)
      list(APPEND death_args_list ${arg_TEST_EXTRA_ARGS})
    endif()
    string(JOIN ";" death_args_joined ${death_args_list})
    string(REPLACE ";" "\\;" death_args_escaped "${death_args_joined}")
    set(death_args_def "-DARGS=${death_args_escaped}")

    set(emu_def "")
    if(NOT "${arg_TEST_EXECUTOR}" STREQUAL "")
      string(JOIN ";" emu_joined ${arg_TEST_EXECUTOR})
      string(REPLACE ";" "\\;" emu_escaped "${emu_joined}")
      set(emu_def "-DEMU=${emu_escaped}")
    endif()

    set(expect_def "")
    if(NOT "${arg_EXPECT_SUBSTRING}" STREQUAL "")
      set(expect_val "${arg_EXPECT_SUBSTRING}")
      string(REPLACE ";" "\\;" expect_val "${expect_val}")
      set(expect_def "-DEXPECT_SUBSTRING=${expect_val}")
    endif()

    string(APPEND script "add_test(${guarded_testname}")
    foreach(arg IN ITEMS
      "${_gentest_cmake_command}"
      "${emu_def}"
      "-DPROG=${arg_TEST_EXECUTABLE}"
      "${death_args_def}"
      "${expect_def}"
      "-P"
      "${_gentest_check_death_script}"
      )
      if(arg STREQUAL "")
        continue()
      endif()
      if(arg MATCHES "[^-./:a-zA-Z0-9_]")
        string(APPEND script " [==[${arg}]==]")
      else()
        string(APPEND script " ${arg}")
      endif()
    endforeach()
    string(APPEND script ")\n")

    _gentest_add_command(set_tests_properties
      "${guarded_testname}"
      PROPERTIES
      WORKING_DIRECTORY "${arg_TEST_WORKING_DIR}"
      SKIP_REGULAR_EXPRESSION "\\[ SKIP \\]"
      ${arg_TEST_PROPERTIES}
    )

    string(REPLACE [[;]] [[\;]] _testname_escaped "${testname}")
    list(APPEND tests "${_testname_escaped}")

    string(LENGTH "${script}" script_len)
    if(script_len GREATER "50000")
      file(${file_write_mode} "${arg_CTEST_FILE}" "${script}")
      set(file_write_mode APPEND)
      set(script "")
    endif()
  endforeach()
  _gentest_add_command(set "" ${arg_TEST_LIST} "${tests}")

  file(${file_write_mode} "${arg_CTEST_FILE}" "${script}")
endfunction()

if(CMAKE_SCRIPT_MODE_FILE)
  gentest_discover_tests_impl(
    TEST_EXECUTABLE ${TEST_EXECUTABLE}
    TEST_EXECUTOR "${TEST_EXECUTOR}"
    TEST_WORKING_DIR ${TEST_WORKING_DIR}
    TEST_PREFIX ${TEST_PREFIX}
    TEST_SUFFIX ${TEST_SUFFIX}
    TEST_FILTER ${TEST_FILTER}
    TEST_LIST ${TEST_LIST}
    CTEST_FILE ${CTEST_FILE}
    TEST_DISCOVERY_TIMEOUT ${TEST_DISCOVERY_TIMEOUT}
    TEST_EXTRA_ARGS "${TEST_EXTRA_ARGS}"
    TEST_DISCOVERY_EXTRA_ARGS "${TEST_DISCOVERY_EXTRA_ARGS}"
    TEST_PROPERTIES "${TEST_PROPERTIES}"
    EXPECT_SUBSTRING ${EXPECT_SUBSTRING}
  )
endif()
]====])
    string(CONFIGURE "${_gentest_script_content}" _gentest_script_content @ONLY)
    file(WRITE "${_gentest_add_tests_script}" "${_gentest_script_content}")

    set(${out_script} "${_gentest_add_tests_script}" PARENT_SCOPE)
endfunction()

function(gentest_discover_tests target)
    set(options "")
    set(one_value_args
        TEST_PREFIX
        TEST_SUFFIX
        TEST_FILTER
        WORKING_DIRECTORY
        TEST_LIST
        DISCOVERY_TIMEOUT
        DISCOVERY_MODE
        EXPECT_SUBSTRING)
    set(multi_value_args EXTRA_ARGS DISCOVERY_EXTRA_ARGS PROPERTIES)
    cmake_parse_arguments(PARSE_ARGV 1 GENTEST "${options}" "${one_value_args}" "${multi_value_args}")

    if(NOT TARGET ${target})
        message(FATAL_ERROR "gentest_discover_tests: target '${target}' does not exist")
    endif()

    get_target_property(_gentest_target_type ${target} TYPE)
    if(NOT _gentest_target_type STREQUAL "EXECUTABLE")
        message(FATAL_ERROR "gentest_discover_tests: target '${target}' must be an executable")
    endif()

    if(NOT GENTEST_WORKING_DIRECTORY)
        set(GENTEST_WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    endif()
    if(NOT GENTEST_TEST_LIST)
        set(GENTEST_TEST_LIST ${target}_TESTS)
    endif()
    if(NOT GENTEST_DISCOVERY_TIMEOUT)
        set(GENTEST_DISCOVERY_TIMEOUT 5)
    endif()
    if(NOT GENTEST_DISCOVERY_MODE)
        if(NOT CMAKE_GENTEST_DISCOVER_TESTS_DISCOVERY_MODE)
            set(CMAKE_GENTEST_DISCOVER_TESTS_DISCOVERY_MODE "POST_BUILD")
        endif()
        set(GENTEST_DISCOVERY_MODE ${CMAKE_GENTEST_DISCOVER_TESTS_DISCOVERY_MODE})
    endif()
    get_property(_gentest_has_counter TARGET ${target} PROPERTY GENTEST_DISCOVERED_TEST_COUNTER SET)
    if(_gentest_has_counter)
        get_property(_gentest_counter TARGET ${target} PROPERTY GENTEST_DISCOVERED_TEST_COUNTER)
        math(EXPR _gentest_counter "${_gentest_counter} + 1")
    else()
        set(_gentest_counter 1)
    endif()
    set_property(TARGET ${target} PROPERTY GENTEST_DISCOVERED_TEST_COUNTER ${_gentest_counter})

    set(_gentest_ctest_file_base "${CMAKE_CURRENT_BINARY_DIR}/${target}[${_gentest_counter}]")
    set(_gentest_ctest_include_file "${_gentest_ctest_file_base}_include.cmake")
    set(_gentest_ctest_tests_file "${_gentest_ctest_file_base}_tests.cmake")

    get_property(_gentest_test_launcher TARGET ${target} PROPERTY TEST_LAUNCHER)
    get_property(_gentest_crosscompiling_emulator TARGET ${target} PROPERTY CROSSCOMPILING_EMULATOR)
    if(_gentest_test_launcher AND _gentest_crosscompiling_emulator)
        set(_gentest_test_executor "${_gentest_test_launcher}" "${_gentest_crosscompiling_emulator}")
    elseif(_gentest_test_launcher)
        set(_gentest_test_executor "${_gentest_test_launcher}")
    elseif(_gentest_crosscompiling_emulator)
        set(_gentest_test_executor "${_gentest_crosscompiling_emulator}")
    else()
        set(_gentest_test_executor "")
    endif()

    _gentest_write_discover_tests_script(_gentest_add_tests_script)

    if(GENTEST_DISCOVERY_MODE STREQUAL "POST_BUILD")
        add_custom_command(
            TARGET ${target} POST_BUILD
            BYPRODUCTS "${_gentest_ctest_tests_file}"
            COMMAND "${CMAKE_COMMAND}"
                -D "TEST_EXECUTABLE=$<TARGET_FILE:${target}>"
                -D "TEST_EXECUTOR=${_gentest_test_executor}"
                -D "TEST_WORKING_DIR=${GENTEST_WORKING_DIRECTORY}"
                -D "TEST_EXTRA_ARGS=${GENTEST_EXTRA_ARGS}"
                -D "TEST_PROPERTIES=${GENTEST_PROPERTIES}"
                -D "TEST_PREFIX=${GENTEST_TEST_PREFIX}"
                -D "TEST_SUFFIX=${GENTEST_TEST_SUFFIX}"
                -D "TEST_FILTER=${GENTEST_TEST_FILTER}"
                -D "TEST_LIST=${GENTEST_TEST_LIST}"
                -D "CTEST_FILE=${_gentest_ctest_tests_file}"
                -D "TEST_DISCOVERY_TIMEOUT=${GENTEST_DISCOVERY_TIMEOUT}"
                -D "TEST_DISCOVERY_EXTRA_ARGS=${GENTEST_DISCOVERY_EXTRA_ARGS}"
                -D "EXPECT_SUBSTRING=${GENTEST_EXPECT_SUBSTRING}"
                -P "${_gentest_add_tests_script}"
            VERBATIM
        )

        file(WRITE "${_gentest_ctest_include_file}"
            "if(EXISTS \"${_gentest_ctest_tests_file}\")\n"
            "  include(\"${_gentest_ctest_tests_file}\")\n"
            "else()\n"
            "  add_test(${target}_NOT_BUILT ${target}_NOT_BUILT)\n"
            "endif()\n"
        )
    elseif(GENTEST_DISCOVERY_MODE STREQUAL "PRE_TEST")
        get_property(_gentest_is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
        if(_gentest_is_multi_config)
            set(_gentest_ctest_tests_file "${_gentest_ctest_file_base}_tests-$<CONFIG>.cmake")
        endif()

        string(CONCAT _gentest_ctest_include_content
            "if(EXISTS \"$<TARGET_FILE:${target}>\")" "\n"
            "  if(NOT EXISTS \"${_gentest_ctest_tests_file}\" OR" "\n"
            "     NOT \"${_gentest_ctest_tests_file}\" IS_NEWER_THAN \"$<TARGET_FILE:${target}>\" OR\n"
            "     NOT \"${_gentest_ctest_tests_file}\" IS_NEWER_THAN \"\${CMAKE_CURRENT_LIST_FILE}\")\n"
            "    include([==[${_gentest_add_tests_script}]==])" "\n"
            "    gentest_discover_tests_impl(" "\n"
            "      TEST_EXECUTABLE [==[$<TARGET_FILE:${target}>]==]" "\n"
            "      TEST_EXECUTOR [==[${_gentest_test_executor}]==]" "\n"
            "      TEST_WORKING_DIR [==[${GENTEST_WORKING_DIRECTORY}]==]" "\n"
            "      TEST_EXTRA_ARGS [==[${GENTEST_EXTRA_ARGS}]==]" "\n"
            "      TEST_PROPERTIES [==[${GENTEST_PROPERTIES}]==]" "\n"
            "      TEST_PREFIX [==[${GENTEST_TEST_PREFIX}]==]" "\n"
            "      TEST_SUFFIX [==[${GENTEST_TEST_SUFFIX}]==]" "\n"
            "      TEST_FILTER [==[${GENTEST_TEST_FILTER}]==]" "\n"
            "      TEST_LIST [==[${GENTEST_TEST_LIST}]==]" "\n"
            "      CTEST_FILE [==[${_gentest_ctest_tests_file}]==]" "\n"
            "      TEST_DISCOVERY_TIMEOUT [==[${GENTEST_DISCOVERY_TIMEOUT}]==]" "\n"
            "      TEST_DISCOVERY_EXTRA_ARGS [==[${GENTEST_DISCOVERY_EXTRA_ARGS}]==]" "\n"
            "      EXPECT_SUBSTRING [==[${GENTEST_EXPECT_SUBSTRING}]==]" "\n"
            "    )" "\n"
            "  endif()" "\n"
            "  include(\"${_gentest_ctest_tests_file}\")" "\n"
            "else()" "\n"
            "  add_test(${target}_NOT_BUILT ${target}_NOT_BUILT)" "\n"
            "endif()" "\n"
        )

        if(_gentest_is_multi_config)
            foreach(_gentest_cfg IN LISTS CMAKE_CONFIGURATION_TYPES)
                file(GENERATE
                    OUTPUT "${_gentest_ctest_file_base}_include-${_gentest_cfg}.cmake"
                    CONTENT "${_gentest_ctest_include_content}"
                    CONDITION $<CONFIG:${_gentest_cfg}>
                )
            endforeach()
            file(WRITE "${_gentest_ctest_include_file}"
                "include(\"${_gentest_ctest_file_base}_include-\${CTEST_CONFIGURATION_TYPE}.cmake\")"
            )
        else()
            file(GENERATE
                OUTPUT "${_gentest_ctest_file_base}_include.cmake"
                CONTENT "${_gentest_ctest_include_content}"
            )
            file(WRITE "${_gentest_ctest_include_file}"
                "include(\"${_gentest_ctest_file_base}_include.cmake\")"
            )
        endif()
    else()
        message(FATAL_ERROR "gentest_discover_tests: unknown DISCOVERY_MODE '${GENTEST_DISCOVERY_MODE}'")
    endif()

    set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${_gentest_ctest_include_file}")
endfunction()
