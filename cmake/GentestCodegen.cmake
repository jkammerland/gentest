include_guard(GLOBAL)

set(_GENTEST_CODEGEN_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")

if(NOT DEFINED GENTEST_CODEGEN_EXECUTABLE)
    set(GENTEST_CODEGEN_EXECUTABLE "" CACHE FILEPATH
        "Path to a host-built gentest_codegen executable used when the in-tree gentest_codegen target is unavailable (e.g. cross-compiling).")
endif()

if(NOT DEFINED GENTEST_CODEGEN_TARGET)
    set(GENTEST_CODEGEN_TARGET "" CACHE STRING
        "CMake target that resolves to a runnable host gentest_codegen (alternative to GENTEST_CODEGEN_EXECUTABLE; cross builds require an imported executable target).")
endif()

if(NOT DEFINED GENTEST_CODEGEN_DEFAULT_CLANG_ARGS)
    set(GENTEST_CODEGEN_DEFAULT_CLANG_ARGS "-Wno-unknown-attributes;-Wno-attributes;-Wno-unknown-warning-option" CACHE STRING
        "Default extra clang arguments for gentest_codegen. Set empty to disable.")
endif()

if(NOT DEFINED GENTEST_CODEGEN_SCAN_DEPS_MODE)
    set(GENTEST_CODEGEN_SCAN_DEPS_MODE "" CACHE STRING
        "Optional gentest_codegen named-module dependency discovery mode override (AUTO, ON, or OFF). Empty keeps the tool default.")
    set_property(CACHE GENTEST_CODEGEN_SCAN_DEPS_MODE PROPERTY STRINGS "" AUTO ON OFF auto on off)
endif()

if(NOT DEFINED GENTEST_CODEGEN_CLANG_SCAN_DEPS)
    set(GENTEST_CODEGEN_CLANG_SCAN_DEPS "" CACHE STRING
        "Optional path to the clang-scan-deps executable used by gentest_codegen for named-module dependency discovery.")
endif()

if(NOT DEFINED GENTEST_CODEGEN_HOST_CLANG)
    set(GENTEST_CODEGEN_HOST_CLANG "" CACHE FILEPATH
        "Optional path to the host Clang executable used by gentest_codegen for Clang-only operations.")
endif()

function(_gentest_get_codegen_cmake_dir out_dir)
    if(DEFINED CMAKE_CURRENT_FUNCTION_LIST_DIR AND NOT "${CMAKE_CURRENT_FUNCTION_LIST_DIR}" STREQUAL "")
        set(${out_dir} "${CMAKE_CURRENT_FUNCTION_LIST_DIR}" PARENT_SCOPE)
    else()
        set(${out_dir} "${_GENTEST_CODEGEN_CMAKE_DIR}" PARENT_SCOPE)
    endif()
endfunction()

function(_gentest_get_host_executable_suffix out_suffix)
    set(_gentest_suffix "${CMAKE_EXECUTABLE_SUFFIX}")
    if(_gentest_suffix STREQUAL "" AND (WIN32 OR CMAKE_HOST_WIN32))
        set(_gentest_suffix ".exe")
    endif()
    set(${out_suffix} "${_gentest_suffix}" PARENT_SCOPE)
endfunction()

function(_gentest_collect_cross_codegen_clang_args out_var)
    set(_gentest_args "")
    if(CMAKE_CROSSCOMPILING)
        if(CMAKE_CXX_COMPILER_TARGET)
            list(APPEND _gentest_args "--target=${CMAKE_CXX_COMPILER_TARGET}")
        elseif(CMAKE_C_COMPILER_TARGET)
            list(APPEND _gentest_args "--target=${CMAKE_C_COMPILER_TARGET}")
        endif()

        if(CMAKE_SYSROOT)
            list(APPEND _gentest_args "--sysroot=${CMAKE_SYSROOT}")
        endif()

        set(_gentest_cross_compiler "")
        if(CMAKE_CXX_COMPILER)
            set(_gentest_cross_compiler "${CMAKE_CXX_COMPILER}")
        elseif(CMAKE_C_COMPILER)
            set(_gentest_cross_compiler "${CMAKE_C_COMPILER}")
        endif()

        if(_gentest_cross_compiler AND IS_ABSOLUTE "${_gentest_cross_compiler}")
            get_filename_component(_gentest_cross_compiler_dir "${_gentest_cross_compiler}" DIRECTORY)
            get_filename_component(_gentest_cross_toolchain_root "${_gentest_cross_compiler_dir}" DIRECTORY)
            if(EXISTS "${_gentest_cross_toolchain_root}")
                list(APPEND _gentest_args "--gcc-toolchain=${_gentest_cross_toolchain_root}")
            endif()
            unset(_gentest_cross_toolchain_root)
            unset(_gentest_cross_compiler_dir)
        endif()
        unset(_gentest_cross_compiler)
    endif()

    set(${out_var} "${_gentest_args}" PARENT_SCOPE)
endfunction()

function(_gentest_collect_common_codegen_clang_args out_var)
    set(_gentest_args -DGENTEST_CODEGEN=1)
    _gentest_collect_cross_codegen_clang_args(_gentest_cross_codegen_clang_args)
    list(APPEND _gentest_args ${_gentest_cross_codegen_clang_args})
    if(GENTEST_CODEGEN_DEFAULT_CLANG_ARGS AND NOT GENTEST_CODEGEN_DEFAULT_CLANG_ARGS STREQUAL "")
        list(APPEND _gentest_args ${GENTEST_CODEGEN_DEFAULT_CLANG_ARGS})
    endif()
    if(GENTEST_CLANG_ARGS)
        list(APPEND _gentest_args ${GENTEST_CLANG_ARGS})
    endif()
    set(${out_var} "${_gentest_args}" PARENT_SCOPE)
endfunction()

function(_gentest_collect_common_codegen_system_include_args out_var)
    set(_gentest_args "")
    set(_gentest_system_includes "${CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES}")
    if(_gentest_system_includes STREQUAL "" AND CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES)
        set(_gentest_system_includes "${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES}")
    endif()
    if(_gentest_system_includes)
        foreach(_gentest_inc_dir ${_gentest_system_includes})
            list(APPEND _gentest_args -isystem "${_gentest_inc_dir}")
        endforeach()
    endif()
    set(${out_var} "${_gentest_args}" PARENT_SCOPE)
endfunction()

function(_gentest_resolve_codegen_clang_scan_deps out_var)
    set(_gentest_clang_scan_deps "${GENTEST_CODEGEN_CLANG_SCAN_DEPS}")
    if("${_gentest_clang_scan_deps}" STREQUAL ""
       AND DEFINED CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS
       AND NOT "${CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS}" STREQUAL ""
       AND NOT "${CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS}" MATCHES "-NOTFOUND$")
        set(_gentest_clang_scan_deps "${CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS}")
    endif()
    set(${out_var} "${_gentest_clang_scan_deps}" PARENT_SCOPE)
endfunction()

function(_gentest_append_codegen_module_context_args command_var target clang_scan_deps)
    set(_gentest_command "${${command_var}}")
    if(NOT "${GENTEST_CODEGEN_SCAN_DEPS_MODE}" STREQUAL "")
        list(APPEND _gentest_command "--scan-deps-mode=${GENTEST_CODEGEN_SCAN_DEPS_MODE}")
    endif()
    if(NOT "${clang_scan_deps}" STREQUAL "")
        list(APPEND _gentest_command --clang-scan-deps "${clang_scan_deps}")
    endif()
    if(NOT "${GENTEST_CODEGEN_HOST_CLANG}" STREQUAL "")
        list(APPEND _gentest_command --host-clang "${GENTEST_CODEGEN_HOST_CLANG}")
    endif()
    list(APPEND _gentest_command
        "$<$<BOOL:$<TARGET_PROPERTY:${target},GENTEST_CODEGEN_EXTERNAL_MODULE_SOURCE_ARGS>>:$<TARGET_PROPERTY:${target},GENTEST_CODEGEN_EXTERNAL_MODULE_SOURCE_ARGS>>")
    set(${command_var} "${_gentest_command}" PARENT_SCOPE)
endfunction()

function(_gentest_make_codegen_command_launcher executable out_command)
    set(_gentest_command "${executable}")
    if(GENTEST_USES_TERMINFO_SHIM AND UNIX AND NOT APPLE AND GENTEST_TERMINFO_SHIM_DIR)
        set(_gentest_ld_library_path "${GENTEST_TERMINFO_SHIM_DIR}")
        if(DEFINED ENV{LD_LIBRARY_PATH} AND NOT "$ENV{LD_LIBRARY_PATH}" STREQUAL "")
            string(APPEND _gentest_ld_library_path ":$ENV{LD_LIBRARY_PATH}")
        endif()
        set(_gentest_command ${CMAKE_COMMAND} -E env
            "LD_LIBRARY_PATH=${_gentest_ld_library_path}"
            "${executable}")
    endif()
    set(${out_command} "${_gentest_command}" PARENT_SCOPE)
endfunction()

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

function(_gentest_expand_explicit_mock_search_root raw_root out_roots)
    set(_gentest_roots "")
    if("${raw_root}" STREQUAL "")
        set(${out_roots} "" PARENT_SCOPE)
        return()
    endif()

    if("${raw_root}" MATCHES "^\\$<BUILD_INTERFACE:(.*)>$")
        set(_gentest_payload "${CMAKE_MATCH_1}")
        foreach(_gentest_payload_root IN LISTS _gentest_payload)
            _gentest_expand_explicit_mock_search_root("${_gentest_payload_root}" _gentest_payload_roots)
            list(APPEND _gentest_roots ${_gentest_payload_roots})
        endforeach()
    elseif("${raw_root}" MATCHES "^\\$<INSTALL_INTERFACE:(.*)>$")
        set(_gentest_payload "${CMAKE_MATCH_1}")
        foreach(_gentest_payload_root IN LISTS _gentest_payload)
            if(IS_ABSOLUTE "${_gentest_payload_root}")
                list(APPEND _gentest_roots "${_gentest_payload_root}")
            endif()
        endforeach()
    elseif("${raw_root}" MATCHES "\\$<")
        message(FATAL_ERROR
            "gentest explicit mock staging does not support include-directory generator expressions other than "
            "BUILD_INTERFACE/INSTALL_INTERFACE. Unsupported root: '${raw_root}'")
    else()
        list(APPEND _gentest_roots "${raw_root}")
    endif()

    list(REMOVE_DUPLICATES _gentest_roots)
    set(${out_roots} "${_gentest_roots}" PARENT_SCOPE)
endfunction()

function(_gentest_stage_explicit_mock_file stage_dir source_file staged_rel out_staged_files)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source_file}")
    string(MD5 _gentest_stage_key "${stage_dir}|${source_file}|${staged_rel}")
    set(_gentest_stage_prop "GENTEST_EXPLICIT_MOCK_STAGE_${_gentest_stage_key}")
    get_property(_gentest_stage_done GLOBAL PROPERTY "${_gentest_stage_prop}")
    if(_gentest_stage_done)
        set(${out_staged_files} "${stage_dir}/${staged_rel}" PARENT_SCOPE)
        return()
    endif()
    set_property(GLOBAL PROPERTY "${_gentest_stage_prop}" TRUE)

    file(READ "${source_file}" _gentest_rewritten_content)
    set(_gentest_staged_files "")
    get_filename_component(_gentest_source_dir "${source_file}" DIRECTORY)
    get_filename_component(_gentest_stage_file_dir "${staged_rel}" DIRECTORY)
    if(_gentest_stage_file_dir STREQUAL "")
        set(_gentest_stage_file_dir ".")
    endif()

    string(MD5 _gentest_search_roots_key "${stage_dir}")
    get_property(_gentest_search_roots GLOBAL PROPERTY "GENTEST_EXPLICIT_MOCK_SEARCH_ROOTS_${_gentest_search_roots_key}")

    string(REGEX MATCHALL "#[ \t]*include[ \t]*([<][^>]+[>]|\"[^\"]+\")" _gentest_include_matches "${_gentest_rewritten_content}")
    foreach(_gentest_include_match IN LISTS _gentest_include_matches)
        if(NOT _gentest_include_match MATCHES "#[ \t]*include[ \t]*[<\"]([^>\"]+)[>\"]")
            continue()
        endif()
        set(_gentest_include_path "${CMAKE_MATCH_1}")
        if(IS_ABSOLUTE "${_gentest_include_path}")
            continue()
        endif()

        set(_gentest_include_abs "")
        set(_gentest_source_candidate "${_gentest_source_dir}/${_gentest_include_path}")
        if(EXISTS "${_gentest_source_candidate}" AND NOT IS_DIRECTORY "${_gentest_source_candidate}")
            file(REAL_PATH "${_gentest_source_candidate}" _gentest_include_abs)
        else()
            foreach(_gentest_search_root_raw IN LISTS _gentest_search_roots)
                _gentest_expand_explicit_mock_search_root("${_gentest_search_root_raw}" _gentest_search_root_candidates)
                foreach(_gentest_search_root IN LISTS _gentest_search_root_candidates)
                    if("${_gentest_search_root}" STREQUAL "")
                        continue()
                    endif()
                    set(_gentest_root_candidate "${_gentest_search_root}/${_gentest_include_path}")
                    if(EXISTS "${_gentest_root_candidate}" AND NOT IS_DIRECTORY "${_gentest_root_candidate}")
                        file(REAL_PATH "${_gentest_root_candidate}" _gentest_include_abs)
                        break()
                    endif()
                endforeach()
                if(NOT "${_gentest_include_abs}" STREQUAL "")
                    break()
                endif()
            endforeach()
        endif()
        if("${_gentest_include_abs}" STREQUAL "")
            continue()
        endif()

        get_filename_component(_gentest_include_name "${_gentest_include_abs}" NAME)
        string(MD5 _gentest_include_hash "${_gentest_include_abs}")
        set(_gentest_dep_rel "deps/${_gentest_include_hash}_${_gentest_include_name}")
        _gentest_stage_explicit_mock_file("${stage_dir}" "${_gentest_include_abs}" "${_gentest_dep_rel}" _gentest_dep_files)
        list(APPEND _gentest_staged_files ${_gentest_dep_files})
        file(RELATIVE_PATH _gentest_dep_include_rel
            "${stage_dir}/${_gentest_stage_file_dir}"
            "${stage_dir}/${_gentest_dep_rel}")
        if(NOT _gentest_include_match MATCHES "^(#[ \t]*include[ \t]*)[<\"][^>\"]+[>\"]$")
            message(FATAL_ERROR "Failed to rewrite staged explicit mock include: ${_gentest_include_match}")
        endif()
        set(_gentest_include_replacement "${CMAKE_MATCH_1}\"${_gentest_dep_include_rel}\"")
        string(REPLACE "${_gentest_include_match}" "${_gentest_include_replacement}"
            _gentest_rewritten_content "${_gentest_rewritten_content}")
    endforeach()
    get_filename_component(_gentest_staged_abs_dir "${stage_dir}/${staged_rel}" DIRECTORY)
    file(MAKE_DIRECTORY "${_gentest_staged_abs_dir}")
    file(WRITE "${stage_dir}/${staged_rel}" "${_gentest_rewritten_content}\n")
    list(APPEND _gentest_staged_files "${stage_dir}/${staged_rel}")
    list(REMOVE_DUPLICATES _gentest_staged_files)
    set(${out_staged_files} "${_gentest_staged_files}" PARENT_SCOPE)
endfunction()

function(_gentest_materialize_explicit_mock_defs output_dir out_defs out_public_files)
    set(_gentest_defs_dir "${output_dir}/defs")
    file(MAKE_DIRECTORY "${_gentest_defs_dir}")

    set(_gentest_materialized_defs "")
    set(_gentest_public_files "")
    foreach(_gentest_def IN LISTS ARGN)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_gentest_def}")
        get_filename_component(_gentest_def_name "${_gentest_def}" NAME)
        string(MD5 _gentest_def_hash "${_gentest_def}")
        set(_gentest_materialized_def "${_gentest_defs_dir}/${_gentest_def_hash}_${_gentest_def_name}")
        _gentest_stage_explicit_mock_file("${_gentest_defs_dir}" "${_gentest_def}" "${_gentest_def_hash}_${_gentest_def_name}" _gentest_staged_files)
        list(APPEND _gentest_materialized_defs "${_gentest_materialized_def}")
        list(APPEND _gentest_public_files ${_gentest_staged_files})
    endforeach()

    list(REMOVE_DUPLICATES _gentest_public_files)
    set(${out_defs} "${_gentest_materialized_defs}" PARENT_SCOPE)
    set(${out_public_files} "${_gentest_public_files}" PARENT_SCOPE)
endfunction()

function(_gentest_materialize_explicit_module_mock_defs output_dir out_defs out_public_files)
    set(_gentest_defs_dir "${output_dir}/defs")
    set(_gentest_root_support_dir "${output_dir}/deps")
    file(MAKE_DIRECTORY "${_gentest_defs_dir}")

    set(_gentest_materialized_defs "")
    set(_gentest_public_files "")
    foreach(_gentest_def IN LISTS ARGN)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_gentest_def}")
        get_filename_component(_gentest_def_name "${_gentest_def}" NAME)
        string(MD5 _gentest_def_hash "${_gentest_def}")
        set(_gentest_materialized_def "${_gentest_defs_dir}/${_gentest_def_hash}_${_gentest_def_name}")
        _gentest_stage_explicit_mock_file("${_gentest_defs_dir}" "${_gentest_def}" "${_gentest_def_hash}_${_gentest_def_name}" _gentest_staged_files)
        list(APPEND _gentest_materialized_defs "${_gentest_materialized_def}")
        list(APPEND _gentest_public_files ${_gentest_staged_files})
        foreach(_gentest_staged_file IN LISTS _gentest_staged_files)
            if(IS_DIRECTORY "${_gentest_staged_file}")
                continue()
            endif()
            if(NOT _gentest_staged_file MATCHES "/defs/deps/")
                continue()
            endif()
            get_filename_component(_gentest_support_name "${_gentest_staged_file}" NAME)
            set(_gentest_root_support_file "${_gentest_root_support_dir}/${_gentest_support_name}")
            file(MAKE_DIRECTORY "${_gentest_root_support_dir}")
            file(COPY_FILE "${_gentest_staged_file}" "${_gentest_root_support_file}" ONLY_IF_DIFFERENT)
            list(APPEND _gentest_public_files "${_gentest_root_support_file}")
        endforeach()
    endforeach()

    list(REMOVE_DUPLICATES _gentest_public_files)
    set(${out_defs} "${_gentest_materialized_defs}" PARENT_SCOPE)
    set(${out_public_files} "${_gentest_public_files}" PARENT_SCOPE)
endfunction()

function(_gentest_append_target_list_property target property)
    get_target_property(_gentest_existing_values ${target} ${property})
    if(NOT _gentest_existing_values OR _gentest_existing_values MATCHES "-NOTFOUND$")
        set(_gentest_existing_values "")
    endif()
    set(_gentest_updated_values ${_gentest_existing_values} ${ARGN})
    list(REMOVE_DUPLICATES _gentest_updated_values)
    set_property(TARGET ${target} PROPERTY ${property} "${_gentest_updated_values}")
endfunction()

function(_gentest_append_target_export_property target property)
    get_target_property(_gentest_export_properties ${target} EXPORT_PROPERTIES)
    if(NOT _gentest_export_properties OR _gentest_export_properties MATCHES "-NOTFOUND$")
        set(_gentest_export_properties "")
    endif()
    list(APPEND _gentest_export_properties "${property}")
    list(REMOVE_DUPLICATES _gentest_export_properties)
    set_property(TARGET ${target} PROPERTY EXPORT_PROPERTIES "${_gentest_export_properties}")
endfunction()

function(_gentest_collect_external_module_source_mappings out_mappings)
    set(_gentest_mappings "")
    foreach(_gentest_dep_target IN LISTS ARGN)
        if(NOT TARGET "${_gentest_dep_target}")
            continue()
        endif()

        get_target_property(_gentest_mock_alias_target "${_gentest_dep_target}" ALIASED_TARGET)
        if(_gentest_mock_alias_target AND NOT _gentest_mock_alias_target STREQUAL "_gentest_mock_alias_target-NOTFOUND")
            set(_gentest_explicit_mock_target_actual "${_gentest_mock_alias_target}")
        else()
            set(_gentest_explicit_mock_target_actual "${_gentest_dep_target}")
        endif()

        get_target_property(_gentest_mock_is_imported "${_gentest_explicit_mock_target_actual}" IMPORTED)
        if(_gentest_mock_is_imported)
            _gentest_collect_imported_explicit_mock_module_sources(
                "${_gentest_explicit_mock_target_actual}"
                _gentest_imported_module_mappings)
            if(_gentest_imported_module_mappings)
                list(APPEND _gentest_mappings ${_gentest_imported_module_mappings})
            endif()
        else()
            get_target_property(_gentest_build_module_sources "${_gentest_explicit_mock_target_actual}" GENTEST_EXPLICIT_MOCK_MODULE_BUILD_SOURCES)
            if(_gentest_build_module_sources AND NOT _gentest_build_module_sources MATCHES "-NOTFOUND$")
                list(APPEND _gentest_mappings ${_gentest_build_module_sources})
            endif()
        endif()
    endforeach()

    list(REMOVE_DUPLICATES _gentest_mappings)
    set(${out_mappings} "${_gentest_mappings}" PARENT_SCOPE)
endfunction()

function(_gentest_append_external_module_source_args_property target)
    set(_gentest_external_args "")
    foreach(_gentest_mapping IN LISTS ARGN)
        list(APPEND _gentest_external_args "--external-module-source=${_gentest_mapping}")
    endforeach()
    if(_gentest_external_args)
        _gentest_append_target_list_property(${target} GENTEST_CODEGEN_EXTERNAL_MODULE_SOURCE_ARGS ${_gentest_external_args})
    endif()
endfunction()

function(_gentest_collect_imported_explicit_mock_module_sources target out_mappings)
    set(_gentest_mappings "")

    get_target_property(_gentest_rel_module_sources "${target}" GENTEST_EXPLICIT_MOCK_MODULE_REL_SOURCES)
    if(_gentest_rel_module_sources AND NOT _gentest_rel_module_sources MATCHES "-NOTFOUND$")
        get_target_property(_gentest_mock_include_dirs "${target}" INTERFACE_INCLUDE_DIRECTORIES)
        if(NOT _gentest_mock_include_dirs OR _gentest_mock_include_dirs MATCHES "-NOTFOUND$")
            set(_gentest_mock_include_dirs "")
        endif()

        foreach(_gentest_rel_mapping IN LISTS _gentest_rel_module_sources)
            if(NOT _gentest_rel_mapping MATCHES "^([^=]+)=(.+)$")
                continue()
            endif()
            set(_gentest_rel_module_name "${CMAKE_MATCH_1}")
            set(_gentest_rel_source "${CMAKE_MATCH_2}")
            foreach(_gentest_mock_include_dir IN LISTS _gentest_mock_include_dirs)
                _gentest_expand_explicit_mock_search_root("${_gentest_mock_include_dir}" _gentest_expanded_mock_include_dirs)
                foreach(_gentest_expanded_mock_include_dir IN LISTS _gentest_expanded_mock_include_dirs)
                    if("${_gentest_expanded_mock_include_dir}" STREQUAL "" OR NOT IS_ABSOLUTE "${_gentest_expanded_mock_include_dir}")
                        continue()
                    endif()
                    set(_gentest_mock_module_source "${_gentest_expanded_mock_include_dir}/${_gentest_rel_source}")
                    if(EXISTS "${_gentest_mock_module_source}" AND NOT IS_DIRECTORY "${_gentest_mock_module_source}")
                        list(APPEND _gentest_mappings
                             "${_gentest_rel_module_name}=${_gentest_mock_module_source}")
                    endif()
                endforeach()
            endforeach()
        endforeach()
    endif()

    if(NOT _gentest_mappings)
        get_target_property(_gentest_imported_module_sets "${target}" INTERFACE_CXX_MODULE_SETS)
        if(_gentest_imported_module_sets AND NOT _gentest_imported_module_sets MATCHES "-NOTFOUND$")
            foreach(_gentest_module_set IN LISTS _gentest_imported_module_sets)
                get_target_property(_gentest_module_files "${target}" CXX_MODULE_SET_${_gentest_module_set})
                if(NOT _gentest_module_files OR _gentest_module_files MATCHES "-NOTFOUND$")
                    continue()
                endif()
                foreach(_gentest_module_file IN LISTS _gentest_module_files)
                    if("${_gentest_module_file}" STREQUAL "" OR IS_DIRECTORY "${_gentest_module_file}" OR NOT EXISTS "${_gentest_module_file}")
                        continue()
                    endif()
                    get_filename_component(_gentest_module_dir "${_gentest_module_file}" DIRECTORY)
                    _gentest_try_extract_module_name("${_gentest_module_file}" _gentest_module_name "${_gentest_module_dir}")
                    if(_gentest_module_name)
                        list(APPEND _gentest_mappings "${_gentest_module_name}=${_gentest_module_file}")
                    endif()
                endforeach()
            endforeach()
        endif()
    endif()

    list(REMOVE_DUPLICATES _gentest_mappings)
    set(${out_mappings} "${_gentest_mappings}" PARENT_SCOPE)
endfunction()

function(_gentest_file_imports_gentest_mock input out_contains)
    _gentest_run_source_inspector("${input}" _gentest_module_name _gentest_imports_gentest_mock ${ARGN})
    set(${out_contains} "${_gentest_imports_gentest_mock}" PARENT_SCOPE)
endfunction()

function(_gentest_resolve_codegen_backend)
    set(one_value_args TARGET OUT_CODEGEN_TARGET OUT_CODEGEN_EXECUTABLE OUT_CODEGEN_BACKEND_KIND)
    cmake_parse_arguments(GENTEST "" "${one_value_args}" "" ${ARGN})

    _gentest_find_installed_codegen_executable(_gentest_installed_codegen)

    set(_gentest_codegen_target "")
    set(_gentest_codegen_executable "")
    set(_gentest_codegen_backend_kind "")
    if(CMAKE_CROSSCOMPILING AND NOT GENTEST_CODEGEN_EXECUTABLE AND NOT GENTEST_CODEGEN_TARGET)
        message(FATAL_ERROR
            "gentest_attach_codegen(${GENTEST_TARGET}): cross-compiling requires a host gentest_codegen. "
            "Set -DGENTEST_CODEGEN_EXECUTABLE=<path> or -DGENTEST_CODEGEN_TARGET=<imported-executable-target>.")
    endif()
    if(GENTEST_CODEGEN_EXECUTABLE)
        if(NOT EXISTS "${GENTEST_CODEGEN_EXECUTABLE}" OR IS_DIRECTORY "${GENTEST_CODEGEN_EXECUTABLE}")
            message(FATAL_ERROR
                "gentest_attach_codegen(${GENTEST_TARGET}): GENTEST_CODEGEN_EXECUTABLE='${GENTEST_CODEGEN_EXECUTABLE}' does not exist "
                "or is not a file")
        endif()
        set(_gentest_codegen_executable "${GENTEST_CODEGEN_EXECUTABLE}")
        set(_gentest_codegen_backend_kind "explicit_executable")
    elseif(GENTEST_CODEGEN_TARGET)
        if(NOT TARGET ${GENTEST_CODEGEN_TARGET})
            message(FATAL_ERROR "gentest_attach_codegen: GENTEST_CODEGEN_TARGET='${GENTEST_CODEGEN_TARGET}' does not exist")
        endif()
        get_target_property(_gentest_target_imported "${GENTEST_CODEGEN_TARGET}" IMPORTED)
        if(CMAKE_CROSSCOMPILING AND NOT _gentest_target_imported)
            message(FATAL_ERROR
                "gentest_attach_codegen(${GENTEST_TARGET}): GENTEST_CODEGEN_TARGET='${GENTEST_CODEGEN_TARGET}' must be an imported "
                "executable target when cross-compiling so the host tool can run during configure-time inspection and build-time "
                "generation. Use -DGENTEST_CODEGEN_EXECUTABLE=<path> for ordinary build-tree targets, or provide an imported host-tool "
                "target.")
        endif()
        set(_gentest_codegen_target "${GENTEST_CODEGEN_TARGET}")
        set(_gentest_codegen_executable $<TARGET_FILE:${GENTEST_CODEGEN_TARGET}>)
        set(_gentest_codegen_backend_kind "explicit_target")
    elseif(TARGET gentest_codegen)
        set(_gentest_codegen_target gentest_codegen)
        set(_gentest_codegen_executable $<TARGET_FILE:gentest_codegen>)
        set(_gentest_codegen_backend_kind "in_tree_target")
    elseif(NOT _gentest_installed_codegen STREQUAL "")
        set(_gentest_codegen_executable "${_gentest_installed_codegen}")
        set(_gentest_codegen_backend_kind "installed_executable")
    else()
        message(FATAL_ERROR
            "gentest_attach_codegen: no gentest code generator available. "
            "Either enable -DGENTEST_BUILD_CODEGEN=ON (native builds) or provide a host tool via "
            "-DGENTEST_CODEGEN_EXECUTABLE=<path> (cross builds).")
    endif()

    set(${GENTEST_OUT_CODEGEN_TARGET} "${_gentest_codegen_target}" PARENT_SCOPE)
    set(${GENTEST_OUT_CODEGEN_EXECUTABLE} "${_gentest_codegen_executable}" PARENT_SCOPE)
    if(GENTEST_OUT_CODEGEN_BACKEND_KIND)
        set(${GENTEST_OUT_CODEGEN_BACKEND_KIND} "${_gentest_codegen_backend_kind}" PARENT_SCOPE)
    endif()
endfunction()

function(_gentest_configure_manifest_mode)
    set(one_value_args TARGET TARGET_ID OUTPUT OUT_OUTPUT OUT_OUTPUT_DIR)
    set(multi_value_args TUS)
    cmake_parse_arguments(GENTEST "" "${one_value_args}" "${multi_value_args}" ${ARGN})

    set(_gentest_output "${GENTEST_OUTPUT}")
    if(NOT _gentest_output)
        set(_gentest_output "${CMAKE_CURRENT_BINARY_DIR}/${GENTEST_TARGET}_generated.cpp")
    endif()

    if("${_gentest_output}" MATCHES "\\$<")
        message(FATAL_ERROR
            "gentest_attach_codegen(${GENTEST_TARGET}): OUTPUT with generator expressions is not supported in manifest mode. "
            "Use a concrete OUTPUT path instead: '${_gentest_output}'")
    else()
        _gentest_normalize_path_and_key("${_gentest_output}" "${CMAKE_CURRENT_BINARY_DIR}" _gentest_output_abs _gentest_output_key)
        _gentest_reserve_unique_owner("GENTEST_CODEGEN_OUTPUT_OWNER" "${_gentest_output_key}" "${GENTEST_TARGET}" _gentest_prev_owner)
        if(_gentest_prev_owner)
            if(NOT _gentest_prev_owner STREQUAL "${GENTEST_TARGET}")
                message(FATAL_ERROR
                    "gentest_attach_codegen(${GENTEST_TARGET}): OUTPUT '${_gentest_output_abs}' is already used by '${_gentest_prev_owner}'. "
                    "Each target must have a unique OUTPUT to avoid generated file clobbering.")
            endif()
            message(FATAL_ERROR
                "gentest_attach_codegen(${GENTEST_TARGET}): OUTPUT '${_gentest_output_abs}' is registered multiple times for the same target. "
                "Call gentest_attach_codegen() once per target and list all SOURCES in that call.")
        endif()

        foreach(_gentest_src IN LISTS GENTEST_TUS)
            _gentest_normalize_path_and_key("${_gentest_src}" "${CMAKE_CURRENT_SOURCE_DIR}" _gentest_src_abs _gentest_src_key)
            if(_gentest_src_key STREQUAL _gentest_output_key)
                message(FATAL_ERROR
                    "gentest_attach_codegen(${GENTEST_TARGET}): OUTPUT '${_gentest_output_abs}' would overwrite a scanned source file '${_gentest_src_abs}'.")
            endif()
        endforeach()
    endif()

    get_filename_component(_gentest_output_dir "${_gentest_output}" DIRECTORY)
    if(_gentest_output_dir STREQUAL "")
        set(_gentest_output_dir "${CMAKE_CURRENT_BINARY_DIR}")
    endif()

    set(${GENTEST_OUT_OUTPUT} "${_gentest_output}" PARENT_SCOPE)
    set(${GENTEST_OUT_OUTPUT_DIR} "${_gentest_output_dir}" PARENT_SCOPE)
endfunction()

function(_gentest_find_installed_codegen_executable out_executable)
    set(_gentest_installed_codegen "")
    if(NOT CMAKE_CROSSCOMPILING)
        _gentest_get_codegen_cmake_dir(_gentest_codegen_cmake_dir)
        _gentest_get_host_executable_suffix(_gentest_host_executable_suffix)
        set(_gentest_codegen_prefixes "")
        if(DEFINED PACKAGE_PREFIX_DIR AND NOT PACKAGE_PREFIX_DIR STREQUAL "")
            list(APPEND _gentest_codegen_prefixes "${PACKAGE_PREFIX_DIR}")
        endif()
        get_filename_component(_gentest_codegen_prefix "${_gentest_codegen_cmake_dir}/../../.." ABSOLUTE)
        list(APPEND _gentest_codegen_prefixes "${_gentest_codegen_prefix}")
        list(REMOVE_DUPLICATES _gentest_codegen_prefixes)

        set(_gentest_codegen_bindirs "")
        if(DEFINED CMAKE_INSTALL_BINDIR AND NOT CMAKE_INSTALL_BINDIR STREQUAL "")
            list(APPEND _gentest_codegen_bindirs "${CMAKE_INSTALL_BINDIR}")
        endif()
        list(APPEND _gentest_codegen_bindirs "bin")
        list(REMOVE_DUPLICATES _gentest_codegen_bindirs)

        foreach(_gentest_prefix IN LISTS _gentest_codegen_prefixes)
            foreach(_gentest_bindir IN LISTS _gentest_codegen_bindirs)
                set(_gentest_candidate "${_gentest_prefix}/${_gentest_bindir}/gentest_codegen${_gentest_host_executable_suffix}")
                if(EXISTS "${_gentest_candidate}" AND NOT IS_DIRECTORY "${_gentest_candidate}")
                    set(_gentest_installed_codegen "${_gentest_candidate}")
                    break()
                endif()
            endforeach()
            if(NOT _gentest_installed_codegen STREQUAL "")
                break()
            endif()
        endforeach()
    endif()

    set(${out_executable} "${_gentest_installed_codegen}" PARENT_SCOPE)
endfunction()

function(_gentest_probe_source_inspector_executable executable out_supported out_reason)
    string(SHA256 _gentest_probe_hash "${executable}")
    get_property(_gentest_cached_support_set GLOBAL PROPERTY "GENTEST_SOURCE_INSPECTOR_SUPPORT_${_gentest_probe_hash}" SET)
    if(_gentest_cached_support_set)
        get_property(_gentest_cached_support GLOBAL PROPERTY "GENTEST_SOURCE_INSPECTOR_SUPPORT_${_gentest_probe_hash}")
        get_property(_gentest_cached_reason GLOBAL PROPERTY "GENTEST_SOURCE_INSPECTOR_REASON_${_gentest_probe_hash}")
        set(${out_supported} "${_gentest_cached_support}" PARENT_SCOPE)
        set(${out_reason} "${_gentest_cached_reason}" PARENT_SCOPE)
        return()
    endif()

    set(_gentest_probe_dir "${CMAKE_BINARY_DIR}/gentest_source_inspector_probe")
    file(MAKE_DIRECTORY "${_gentest_probe_dir}")
    set(_gentest_probe_source "${_gentest_probe_dir}/${_gentest_probe_hash}.cppm")
    file(WRITE "${_gentest_probe_source}" "module;\nexport module gentest.probe;\nimport gentest.mock;\n")
    _gentest_make_codegen_command_launcher("${executable}" _gentest_probe_command)
    execute_process(
        COMMAND ${_gentest_probe_command} --inspect-source "${_gentest_probe_source}"
        RESULT_VARIABLE _gentest_probe_rc
        OUTPUT_VARIABLE _gentest_probe_out
        ERROR_VARIABLE _gentest_probe_err
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE)
    set(_gentest_probe_combined "${_gentest_probe_out}\n${_gentest_probe_err}")

    set(_gentest_probe_supported FALSE)
    set(_gentest_probe_reason "")
    if(_gentest_probe_rc EQUAL 0
       AND _gentest_probe_combined MATCHES "(^|[\n\r])module_name=gentest\\.probe([\n\r]|$)"
       AND _gentest_probe_combined MATCHES "(^|[\n\r])imports_gentest_mock=1([\n\r]|$)")
        set(_gentest_probe_supported TRUE)
    else()
        set(_gentest_probe_reason
            "candidate '${executable}' is not inspect-capable for configure-time source analysis")
    endif()

    set_property(GLOBAL PROPERTY "GENTEST_SOURCE_INSPECTOR_SUPPORT_${_gentest_probe_hash}" "${_gentest_probe_supported}")
    set_property(GLOBAL PROPERTY "GENTEST_SOURCE_INSPECTOR_REASON_${_gentest_probe_hash}" "${_gentest_probe_reason}")

    set(${out_supported} "${_gentest_probe_supported}" PARENT_SCOPE)
    set(${out_reason} "${_gentest_probe_reason}" PARENT_SCOPE)
endfunction()

function(_gentest_try_get_imported_executable_location target out_executable)
    set(_gentest_imported_executable "")
    set(_gentest_imported_location_props "")
    set(_gentest_imported_configs_to_try "")
    if(DEFINED CMAKE_BUILD_TYPE AND NOT "${CMAKE_BUILD_TYPE}" STREQUAL "")
        string(TOUPPER "${CMAKE_BUILD_TYPE}" _gentest_requested_config_upper)
        get_target_property(_gentest_mapped_requested_configs "${target}" "MAP_IMPORTED_CONFIG_${_gentest_requested_config_upper}")
        if(_gentest_mapped_requested_configs AND NOT _gentest_mapped_requested_configs MATCHES "-NOTFOUND$")
            list(APPEND _gentest_imported_configs_to_try ${_gentest_mapped_requested_configs})
        endif()
        list(APPEND _gentest_imported_configs_to_try "${CMAKE_BUILD_TYPE}")
    endif()

    get_target_property(_gentest_imported_configs "${target}" IMPORTED_CONFIGURATIONS)
    if(_gentest_imported_configs)
        list(APPEND _gentest_imported_configs_to_try ${_gentest_imported_configs})
    endif()
    list(REMOVE_DUPLICATES _gentest_imported_configs_to_try)

    foreach(_gentest_imported_config IN LISTS _gentest_imported_configs_to_try)
        if("${_gentest_imported_config}" STREQUAL "")
            continue()
        endif()
        string(TOUPPER "${_gentest_imported_config}" _gentest_imported_config_upper)
        list(APPEND _gentest_imported_location_props "IMPORTED_LOCATION_${_gentest_imported_config_upper}")
    endforeach()
    list(APPEND _gentest_imported_location_props IMPORTED_LOCATION)
    list(REMOVE_DUPLICATES _gentest_imported_location_props)

    foreach(_gentest_imported_location_prop IN LISTS _gentest_imported_location_props)
        get_target_property(_gentest_imported_candidate "${target}" "${_gentest_imported_location_prop}")
        if(_gentest_imported_candidate
           AND NOT _gentest_imported_candidate STREQUAL "_gentest_imported_candidate-NOTFOUND")
            set(_gentest_imported_executable "${_gentest_imported_candidate}")
            break()
        endif()
    endforeach()

    set(${out_executable} "${_gentest_imported_executable}" PARENT_SCOPE)
endfunction()

function(_gentest_try_resolve_source_inspector_target target out_executable out_reason)
    set(_gentest_target_name "${target}")
    get_target_property(_gentest_aliased_target "${_gentest_target_name}" ALIASED_TARGET)
    if(_gentest_aliased_target)
        set(_gentest_target_name "${_gentest_aliased_target}")
    endif()

    get_target_property(_gentest_target_type "${_gentest_target_name}" TYPE)
    if(NOT _gentest_target_type STREQUAL "EXECUTABLE")
        set(${out_executable} "" PARENT_SCOPE)
        set(${out_reason}
            "candidate target '${target}' is not an executable target and cannot run configure-time source inspection"
            PARENT_SCOPE)
        return()
    endif()

    get_target_property(_gentest_target_imported "${_gentest_target_name}" IMPORTED)
    if(_gentest_target_imported)
        _gentest_try_get_imported_executable_location("${_gentest_target_name}" _gentest_target_executable)
        if(_gentest_target_executable STREQUAL "")
            set(${out_executable} "" PARENT_SCOPE)
            set(${out_reason}
                "candidate target '${target}' does not expose an imported executable location for configure-time source inspection"
                PARENT_SCOPE)
            return()
        endif()
        if(NOT EXISTS "${_gentest_target_executable}" OR IS_DIRECTORY "${_gentest_target_executable}")
            set(${out_executable} "" PARENT_SCOPE)
            set(${out_reason}
                "candidate target '${target}' resolves to '${_gentest_target_executable}', which does not exist or is not a file"
                PARENT_SCOPE)
            return()
        endif()

        set(${out_executable} "${_gentest_target_executable}" PARENT_SCOPE)
        set(${out_reason} "" PARENT_SCOPE)
        return()
    endif()

    set(${out_executable} "" PARENT_SCOPE)
    set(${out_reason}
        "candidate target '${target}' is a build-tree executable and is not runnable during configure-time source inspection. "
        "Configure-time source inspection is only used for explicit module mock metadata; provide a built/installed "
        "gentest_codegen via GENTEST_CODEGEN_EXECUTABLE or an imported GENTEST_CODEGEN_TARGET."
        PARENT_SCOPE)
endfunction()

function(_gentest_try_select_source_inspector_backend out_executable out_reason)
    set(one_value_args BACKEND_KIND BACKEND_EXECUTABLE BACKEND_TARGET)
    cmake_parse_arguments(GENTEST "" "${one_value_args}" "" ${ARGN})

    set(_gentest_selected_executable "")
    set(_gentest_selected_reason "")

    if(GENTEST_BACKEND_KIND STREQUAL "explicit_executable" OR GENTEST_BACKEND_KIND STREQUAL "installed_executable")
        if(NOT EXISTS "${GENTEST_BACKEND_EXECUTABLE}" OR IS_DIRECTORY "${GENTEST_BACKEND_EXECUTABLE}")
            set(_gentest_selected_reason
                "candidate '${GENTEST_BACKEND_EXECUTABLE}' does not exist or is not a file")
        else()
            _gentest_probe_source_inspector_executable("${GENTEST_BACKEND_EXECUTABLE}" _gentest_source_inspector_supported
                _gentest_source_inspector_reason)
            if(_gentest_source_inspector_supported)
                set(_gentest_selected_executable "${GENTEST_BACKEND_EXECUTABLE}")
            else()
                set(_gentest_selected_reason "${_gentest_source_inspector_reason}")
            endif()
        endif()
    elseif(GENTEST_BACKEND_KIND STREQUAL "explicit_target")
        if(NOT TARGET "${GENTEST_BACKEND_TARGET}")
            set(_gentest_selected_reason "candidate target '${GENTEST_BACKEND_TARGET}' does not exist")
        else()
            _gentest_try_resolve_source_inspector_target("${GENTEST_BACKEND_TARGET}" _gentest_target_codegen _gentest_target_reason)
            if(NOT _gentest_target_codegen STREQUAL "")
                _gentest_probe_source_inspector_executable("${_gentest_target_codegen}" _gentest_source_inspector_supported
                    _gentest_source_inspector_reason)
                if(_gentest_source_inspector_supported)
                    set(_gentest_selected_executable "${_gentest_target_codegen}")
                else()
                    set(_gentest_selected_reason
                        "candidate target '${GENTEST_BACKEND_TARGET}' resolves to '${_gentest_target_codegen}', but ${_gentest_source_inspector_reason}")
                endif()
            else()
                set(_gentest_selected_reason "${_gentest_target_reason}")
            endif()
        endif()
    elseif(GENTEST_BACKEND_KIND STREQUAL "in_tree_target")
        set(_gentest_selected_reason
            "candidate target '${GENTEST_BACKEND_TARGET}' is a build-tree executable and is not runnable during configure-time source inspection. "
            "Configure-time source inspection is only used for explicit module mock metadata; provide a built/installed "
            "gentest_codegen via GENTEST_CODEGEN_EXECUTABLE or an imported GENTEST_CODEGEN_TARGET.")
    endif()

    set(${out_executable} "${_gentest_selected_executable}" PARENT_SCOPE)
    set(${out_reason} "${_gentest_selected_reason}" PARENT_SCOPE)
endfunction()

function(_gentest_resolve_source_inspector_executable out_executable)
    set(_gentest_source_inspector_backend_kind "")
    set(_gentest_source_inspector_backend_executable "")
    set(_gentest_source_inspector_backend_target "")

    if(GENTEST_CODEGEN_EXECUTABLE)
        set(_gentest_source_inspector_backend_kind "explicit_executable")
        set(_gentest_source_inspector_backend_executable "${GENTEST_CODEGEN_EXECUTABLE}")
    elseif(GENTEST_CODEGEN_TARGET)
        set(_gentest_source_inspector_backend_kind "explicit_target")
        set(_gentest_source_inspector_backend_target "${GENTEST_CODEGEN_TARGET}")
    else()
        _gentest_find_installed_codegen_executable(_gentest_installed_codegen)
        if(NOT _gentest_installed_codegen STREQUAL "")
            set(_gentest_source_inspector_backend_kind "installed_executable")
            set(_gentest_source_inspector_backend_executable "${_gentest_installed_codegen}")
        elseif(TARGET gentest_codegen)
            set(_gentest_source_inspector_backend_kind "in_tree_target")
            set(_gentest_source_inspector_backend_target gentest_codegen)
        endif()
    endif()

    if(_gentest_source_inspector_backend_kind STREQUAL "")
        if(CMAKE_CROSSCOMPILING)
            message(FATAL_ERROR
                "gentest source inspection for explicit module mock metadata requires a runnable host gentest_codegen during configure time. "
                "Set -DGENTEST_CODEGEN_EXECUTABLE=<path> or -DGENTEST_CODEGEN_TARGET=<imported-executable-target> when cross-compiling.")
        endif()

        message(FATAL_ERROR
            "gentest source inspection for explicit module mock metadata could not resolve a runnable gentest_codegen executable. "
            "Source-tree gentest_codegen targets are build outputs and cannot be used during configure; use an installed tool "
            "or set -DGENTEST_CODEGEN_EXECUTABLE=<path>.")
    endif()

    _gentest_try_select_source_inspector_backend(
        _gentest_source_inspector_executable
        _gentest_source_inspector_reason
        BACKEND_KIND "${_gentest_source_inspector_backend_kind}"
        BACKEND_EXECUTABLE "${_gentest_source_inspector_backend_executable}"
        BACKEND_TARGET "${_gentest_source_inspector_backend_target}")
    if(NOT _gentest_source_inspector_executable STREQUAL "")
        set(${out_executable} "${_gentest_source_inspector_executable}" PARENT_SCOPE)
        return()
    endif()

    message(FATAL_ERROR
        "gentest source inspection for explicit module mock metadata could not resolve a runnable inspect-capable backend.\n"
        "  - ${_gentest_source_inspector_reason}")
endfunction()

function(_gentest_run_source_inspector input out_module_name out_imports_mock)
    _gentest_resolve_source_inspector_executable(_gentest_source_inspector_executable)

    set(_gentest_inspector_args ${ARGN})
    set(_gentest_inspect_include_dirs "")
    set(_gentest_inspect_command_line "")
    set(_gentest_collect_command_line FALSE)
    foreach(_gentest_arg IN LISTS _gentest_inspector_args)
        if(_gentest_arg STREQUAL "__GENTEST_SCAN_COMMAND_LINE__")
            set(_gentest_collect_command_line TRUE)
            continue()
        endif()
        if(_gentest_collect_command_line)
            list(APPEND _gentest_inspect_command_line "${_gentest_arg}")
        else()
            list(APPEND _gentest_inspect_include_dirs "${_gentest_arg}")
        endif()
    endforeach()

    set(_gentest_inspect_command
        )
    _gentest_make_codegen_command_launcher("${_gentest_source_inspector_executable}" _gentest_inspect_command)
    list(APPEND _gentest_inspect_command
        --inspect-source)
    foreach(_gentest_include_dir IN LISTS _gentest_inspect_include_dirs)
        if(NOT "${_gentest_include_dir}" STREQUAL "")
            list(APPEND _gentest_inspect_command --inspect-include-dir "${_gentest_include_dir}")
        endif()
    endforeach()
    list(APPEND _gentest_inspect_command "${input}")
    if(_gentest_inspect_command_line)
        list(APPEND _gentest_inspect_command -- ${_gentest_inspect_command_line})
    endif()

    execute_process(
        COMMAND ${_gentest_inspect_command}
        RESULT_VARIABLE _gentest_inspect_rc
        OUTPUT_VARIABLE _gentest_inspect_out
        ERROR_VARIABLE _gentest_inspect_err
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE)
    if(NOT _gentest_inspect_rc EQUAL 0)
        message(FATAL_ERROR
            "gentest source inspection failed for '${input}'.\n"
            "--- stdout ---\n${_gentest_inspect_out}\n--- stderr ---\n${_gentest_inspect_err}")
    endif()
    if(NOT _gentest_inspect_err STREQUAL "")
        message(NOTICE "${_gentest_inspect_err}")
    endif()

    set(_gentest_module_name "")
    string(REGEX MATCH "module_name=([^\n\r]*)" _gentest_module_match "${_gentest_inspect_out}")
    if(NOT _gentest_module_match)
        message(FATAL_ERROR
            "gentest source inspection returned malformed output for '${input}': missing module_name=...\n"
            "--- stdout ---\n${_gentest_inspect_out}\n--- stderr ---\n${_gentest_inspect_err}")
    endif()
    set(_gentest_module_name "${CMAKE_MATCH_1}")

    set(_gentest_imports_mock FALSE)
    string(REGEX MATCH "imports_gentest_mock=([01])" _gentest_imports_match "${_gentest_inspect_out}")
    if(NOT _gentest_imports_match)
        message(FATAL_ERROR
            "gentest source inspection returned malformed output for '${input}': missing imports_gentest_mock=...\n"
            "--- stdout ---\n${_gentest_inspect_out}\n--- stderr ---\n${_gentest_inspect_err}")
    endif()
    if(CMAKE_MATCH_1 STREQUAL "1")
        set(_gentest_imports_mock TRUE)
    endif()

    set(${out_module_name} "${_gentest_module_name}" PARENT_SCOPE)
    set(${out_imports_mock} "${_gentest_imports_mock}" PARENT_SCOPE)
endfunction()

function(_gentest_extract_module_name input out_name)
    _gentest_try_extract_module_name("${input}" _gentest_module_name ${ARGN})

    if(_gentest_module_name STREQUAL "")
        message(FATAL_ERROR
            "gentest_attach_codegen: unable to determine module name for '${input}'. "
            "Expected a named module declaration like 'export module name;' or 'module name;'.")
    endif()

    set(${out_name} "${_gentest_module_name}" PARENT_SCOPE)
endfunction()

function(_gentest_try_extract_module_name input out_name)
    _gentest_run_source_inspector("${input}" _gentest_module_name _gentest_imports_gentest_mock ${ARGN})
    set(${out_name} "${_gentest_module_name}" PARENT_SCOPE)
endfunction()

function(_gentest_make_mock_domain_output_path input_path idx label out_path)
    get_filename_component(_gentest_domain_dir "${input_path}" DIRECTORY)
    get_filename_component(_gentest_domain_stem "${input_path}" NAME_WE)
    get_filename_component(_gentest_domain_ext "${input_path}" EXT)

    string(REGEX REPLACE "[^A-Za-z0-9_]" "_" _gentest_domain_label "${label}")
    if(_gentest_domain_label STREQUAL "")
        set(_gentest_domain_label "domain")
    endif()
    if(NOT _gentest_domain_label STREQUAL "header")
        string(LENGTH "${_gentest_domain_label}" _gentest_domain_label_len)
        if(_gentest_domain_label_len GREATER 32)
            string(SUBSTRING "${_gentest_domain_label}" 0 16 _gentest_domain_prefix)
            string(SHA256 _gentest_domain_hash "${_gentest_domain_label}")
            string(SUBSTRING "${_gentest_domain_hash}" 0 8 _gentest_domain_hash8)
            set(_gentest_domain_label "${_gentest_domain_prefix}_${_gentest_domain_hash8}")
        endif()
    endif()

    set(_gentest_idx_str "${idx}")
    string(LENGTH "${_gentest_idx_str}" _gentest_idx_len)
    if(_gentest_idx_len LESS 4)
        math(EXPR _gentest_pad "4 - ${_gentest_idx_len}")
        string(REPEAT "0" ${_gentest_pad} _gentest_zeros)
        set(_gentest_idx_str "${_gentest_zeros}${_gentest_idx_str}")
    endif()

    set(${out_path}
        "${_gentest_domain_dir}/${_gentest_domain_stem}__domain_${_gentest_idx_str}_${_gentest_domain_label}${_gentest_domain_ext}"
        PARENT_SCOPE)
endfunction()

function(_gentest_make_module_wrapper_output_path output_dir input_tu idx out_path)
    get_filename_component(_gentest_module_stem "${input_tu}" NAME_WE)
    string(REGEX REPLACE "[^A-Za-z0-9_]" "_" _gentest_module_stem "${_gentest_module_stem}")
    if(_gentest_module_stem STREQUAL "")
        set(_gentest_module_stem "tu")
    endif()
    _gentest_shorten_generated_stem("${_gentest_module_stem}" _gentest_module_stem)
    get_filename_component(_gentest_module_ext "${input_tu}" EXT)

    set(_gentest_idx_str "${idx}")
    string(LENGTH "${_gentest_idx_str}" _gentest_idx_len)
    if(_gentest_idx_len LESS 4)
        math(EXPR _gentest_pad "4 - ${_gentest_idx_len}")
        string(REPEAT "0" ${_gentest_pad} _gentest_zeros)
        set(_gentest_idx_str "${_gentest_zeros}${_gentest_idx_str}")
    endif()

    set(${out_path}
        "${output_dir}/tu_${_gentest_idx_str}_${_gentest_module_stem}.module.gentest${_gentest_module_ext}"
        PARENT_SCOPE)
endfunction()

function(_gentest_make_module_registration_output_path output_dir input_tu idx out_path)
    get_filename_component(_gentest_module_stem "${input_tu}" NAME_WE)
    string(REGEX REPLACE "[^A-Za-z0-9_]" "_" _gentest_module_stem "${_gentest_module_stem}")
    if(_gentest_module_stem STREQUAL "")
        set(_gentest_module_stem "tu")
    endif()
    _gentest_shorten_generated_stem("${_gentest_module_stem}" _gentest_module_stem)

    set(_gentest_idx_str "${idx}")
    string(LENGTH "${_gentest_idx_str}" _gentest_idx_len)
    if(_gentest_idx_len LESS 4)
        math(EXPR _gentest_pad "4 - ${_gentest_idx_len}")
        string(REPEAT "0" ${_gentest_pad} _gentest_zeros)
        set(_gentest_idx_str "${_gentest_zeros}${_gentest_idx_str}")
    endif()

    set(${out_path}
        "${output_dir}/tu_${_gentest_idx_str}_${_gentest_module_stem}.registration.gentest.cpp"
        PARENT_SCOPE)
endfunction()

function(_gentest_shorten_generated_stem input_stem out_stem)
    set(_gentest_short_stem "${input_stem}")
    string(LENGTH "${_gentest_short_stem}" _gentest_short_stem_len)
    if(_gentest_short_stem_len GREATER 24)
        string(SUBSTRING "${_gentest_short_stem}" 0 16 _gentest_short_stem_prefix)
        string(MD5 _gentest_short_stem_hash "${_gentest_short_stem}")
        string(SUBSTRING "${_gentest_short_stem_hash}" 0 8 _gentest_short_stem_hash8)
        set(_gentest_short_stem "${_gentest_short_stem_prefix}_${_gentest_short_stem_hash8}")
    endif()
    set(${out_stem} "${_gentest_short_stem}" PARENT_SCOPE)
endfunction()

function(_gentest_copy_source_properties_to_wrappers)
    set(multi_value_args TU_SOURCE_ENTRIES TUS WRAPPER_CPP EXTRA_CPP)
    cmake_parse_arguments(GENTEST "" "" "${multi_value_args}" ${ARGN})

    set(_gentest_source_props COMPILE_DEFINITIONS COMPILE_OPTIONS INCLUDE_DIRECTORIES COMPILE_FLAGS CXX_SCAN_FOR_MODULES)
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

    list(LENGTH GENTEST_WRAPPER_CPP _gentest_wrapper_count)
    math(EXPR _gentest_last "${_gentest_wrapper_count} - 1")
    foreach(_idx RANGE 0 ${_gentest_last})
        list(GET GENTEST_TU_SOURCE_ENTRIES ${_idx} _orig_entry)
        list(GET GENTEST_TUS ${_idx} _orig_abs)
        list(GET GENTEST_WRAPPER_CPP ${_idx} _wrap_cpp)

        foreach(_prop IN LISTS _gentest_source_props)
            get_source_file_property(_val "${_orig_entry}" ${_prop})
            if(_val STREQUAL "NOTFOUND")
                get_source_file_property(_val "${_orig_abs}" ${_prop})
            endif()
            if(NOT _val STREQUAL "NOTFOUND")
                set_source_files_properties("${_wrap_cpp}" PROPERTIES ${_prop} "${_val}")
            endif()
        endforeach()

        get_filename_component(_orig_dir "${_orig_abs}" DIRECTORY)
        if(NOT _orig_dir STREQUAL "")
            get_source_file_property(_wrap_include_dirs "${_wrap_cpp}" INCLUDE_DIRECTORIES)
            if(_wrap_include_dirs STREQUAL "NOTFOUND" OR _wrap_include_dirs STREQUAL "")
                set(_wrap_include_dirs "${_orig_dir}")
            else()
                list(APPEND _wrap_include_dirs "${_orig_dir}")
                list(REMOVE_DUPLICATES _wrap_include_dirs)
            endif()
            set_source_files_properties("${_wrap_cpp}" PROPERTIES INCLUDE_DIRECTORIES "${_wrap_include_dirs}")
        endif()

        if(GENTEST_EXTRA_CPP)
            list(GET GENTEST_EXTRA_CPP ${_idx} _extra_cpp)
            if(NOT _extra_cpp STREQUAL "__gentest_no_registration__")
                foreach(_prop IN LISTS _gentest_source_props)
                    get_source_file_property(_val "${_orig_entry}" ${_prop})
                    if(_val STREQUAL "NOTFOUND")
                        get_source_file_property(_val "${_orig_abs}" ${_prop})
                    endif()
                    if(NOT _val STREQUAL "NOTFOUND")
                        set_source_files_properties("${_extra_cpp}" PROPERTIES ${_prop} "${_val}")
                    endif()
                endforeach()

                if(NOT _orig_dir STREQUAL "")
                    get_source_file_property(_extra_include_dirs "${_extra_cpp}" INCLUDE_DIRECTORIES)
                    if(_extra_include_dirs STREQUAL "NOTFOUND" OR _extra_include_dirs STREQUAL "")
                        set(_extra_include_dirs "${_orig_dir}")
                    else()
                        list(APPEND _extra_include_dirs "${_orig_dir}")
                        list(REMOVE_DUPLICATES _extra_include_dirs)
                    endif()
                    set_source_files_properties("${_extra_cpp}" PROPERTIES INCLUDE_DIRECTORIES "${_extra_include_dirs}")
                endif()
            endif()
        endif()
    endforeach()
endfunction()

function(_gentest_prepare_tu_mode)
    set(one_value_args
        TARGET TARGET_ID OUTPUT_DIR NO_INCLUDE_SOURCES
        OUT_OUTPUT_DIR OUT_WRAPPER_CPP OUT_WRAPPER_HEADERS OUT_EXTRA_CPP
        OUT_ARTIFACT_MANIFEST OUT_COMPILE_CONTEXT_IDS OUT_ARTIFACT_OWNER_SOURCES)
    set(multi_value_args TUS TU_SOURCE_ENTRIES MODULE_NAMES NEEDS_MODULE_SCAN)
    cmake_parse_arguments(GENTEST "" "${one_value_args}" "${multi_value_args}" ${ARGN})

    set(_gentest_requires_includes FALSE)
    foreach(_gentest_module_name IN LISTS GENTEST_MODULE_NAMES)
        if(_gentest_module_name STREQUAL "__gentest_no_module__")
            set(_gentest_requires_includes TRUE)
            break()
        endif()
    endforeach()

    if(GENTEST_NO_INCLUDE_SOURCES AND _gentest_requires_includes)
        message(FATAL_ERROR
            "gentest_attach_codegen(${GENTEST_TARGET}): NO_INCLUDE_SOURCES is not supported in TU wrapper mode, "
            "because wrappers must include the original translation unit. "
            "Use OUTPUT=... to switch to legacy manifest mode if you need NO_INCLUDE_SOURCES.")
    endif()

    if(GENTEST_OUTPUT_DIR)
        set(_gentest_output_dir "${GENTEST_OUTPUT_DIR}")
    else()
        set(_gentest_output_dir "${CMAKE_CURRENT_BINARY_DIR}/gentest/${GENTEST_TARGET_ID}")
    endif()

    if("${_gentest_output_dir}" MATCHES "\\$<")
        message(FATAL_ERROR
            "gentest_attach_codegen(${GENTEST_TARGET}): OUTPUT_DIR contains generator expressions, which is not supported in TU wrapper mode "
            "(requires a concrete directory to generate shim translation units). "
            "Pass a concrete OUTPUT_DIR, or use OUTPUT=... to switch to manifest mode.")
    endif()

    _gentest_normalize_path_and_key("${_gentest_output_dir}" "${CMAKE_CURRENT_BINARY_DIR}" _gentest_outdir_abs _gentest_outdir_key)
    set(_gentest_output_dir "${_gentest_outdir_abs}")

    _gentest_reserve_unique_owner("GENTEST_CODEGEN_OUTDIR_OWNER" "${_gentest_outdir_key}" "${GENTEST_TARGET}" _gentest_prev_owner)
    if(_gentest_prev_owner AND NOT _gentest_prev_owner STREQUAL "${GENTEST_TARGET}")
        message(FATAL_ERROR
            "gentest_attach_codegen(${GENTEST_TARGET}): OUTPUT_DIR '${_gentest_outdir_abs}' is already used by '${_gentest_prev_owner}'. "
            "Each target should have a unique OUTPUT_DIR to avoid generated file clobbering.")
    endif()

    set(_gentest_wrapper_cpp "")
    set(_gentest_wrapper_headers "")
    set(_gentest_registration_cpp "")
    set(_gentest_compile_context_ids "")
    set(_gentest_artifact_owner_sources "")
    list(LENGTH GENTEST_TUS _gentest_tu_count)
    math(EXPR _gentest_last_tu "${_gentest_tu_count} - 1")
    foreach(_gentest_idx RANGE 0 ${_gentest_last_tu})
        list(GET GENTEST_TUS ${_gentest_idx} _tu)
        list(GET GENTEST_MODULE_NAMES ${_gentest_idx} _module_name)
        get_filename_component(_stem "${_tu}" NAME_WE)
        string(REGEX REPLACE "[^A-Za-z0-9_]" "_" _stem "${_stem}")
        if(_stem STREQUAL "")
            set(_stem "tu")
        endif()
        _gentest_shorten_generated_stem("${_stem}" _stem)
        set(_idx_str "${_gentest_idx}")
        string(LENGTH "${_idx_str}" _idx_len)
        if(_idx_len LESS 4)
            math(EXPR _pad "4 - ${_idx_len}")
            string(REPEAT "0" ${_pad} _zeros)
            set(_idx_str "${_zeros}${_idx_str}")
        endif()
        list(APPEND _gentest_wrapper_headers "${_gentest_output_dir}/tu_${_idx_str}_${_stem}.gentest.h")
        if(_module_name STREQUAL "__gentest_no_module__")
            list(APPEND _gentest_wrapper_cpp "${_gentest_output_dir}/tu_${_idx_str}_${_stem}.gentest.cpp")
            list(APPEND _gentest_registration_cpp "__gentest_no_registration__")
        else()
            _gentest_make_module_wrapper_output_path("${_gentest_output_dir}" "${_tu}" ${_gentest_idx} _gentest_module_wrap_cpp)
            list(APPEND _gentest_wrapper_cpp "${_gentest_module_wrap_cpp}")
            list(APPEND _gentest_registration_cpp "__gentest_no_registration__")
        endif()
        list(APPEND _gentest_compile_context_ids "${GENTEST_TARGET_ID}:${_tu}")
        list(APPEND _gentest_artifact_owner_sources "${_tu}")
    endforeach()

    file(MAKE_DIRECTORY "${_gentest_output_dir}")

    set(_gentest_module_generated_sources "")
    set(_gentest_extra_cpp "")
    list(LENGTH _gentest_wrapper_cpp _gentest_wrapper_count)
    math(EXPR _gentest_last "${_gentest_wrapper_count} - 1")
    foreach(_idx RANGE 0 ${_gentest_last})
        list(GET GENTEST_TUS ${_idx} _orig_abs)
        list(GET _gentest_wrapper_cpp ${_idx} _wrap_cpp)
        list(GET _gentest_wrapper_headers ${_idx} _wrap_header)
        list(GET _gentest_registration_cpp ${_idx} _reg_cpp)
        list(GET GENTEST_MODULE_NAMES ${_idx} _module_name)
        list(GET GENTEST_NEEDS_MODULE_SCAN ${_idx} _needs_module_scan)
        get_filename_component(_wrap_header_name "${_wrap_header}" NAME)

        if(_module_name STREQUAL "__gentest_no_module__")
            file(RELATIVE_PATH _rel_src "${_gentest_output_dir}" "${_orig_abs}")
            string(REPLACE "\\" "/" _rel_src "${_rel_src}")

            set(_shim_content
"// This file is auto-generated by gentest (CMake shim).\n\
// Do not edit manually.\n\
\n\
// Include the original translation unit so fixture types and test bodies are\n\
// visible for wrappers.\n\
// NOLINTNEXTLINE(bugprone-suspicious-include)\n\
#include \"${_rel_src}\"\n\
\n\
// Include generated registrations after the original TU is visible.\n\
// During codegen or module-dependency scans, this header may not exist yet.\n\
#if !defined(GENTEST_CODEGEN) && __has_include(\"${_wrap_header_name}\")\n\
#include \"${_wrap_header_name}\"\n\
#endif\n")

            file(GENERATE OUTPUT "${_wrap_cpp}" CONTENT "${_shim_content}")
            if(_needs_module_scan)
                set_source_files_properties("${_wrap_cpp}" PROPERTIES OBJECT_DEPENDS "${_wrap_header}" CXX_SCAN_FOR_MODULES ON)
            else()
                set_source_files_properties("${_wrap_cpp}" PROPERTIES OBJECT_DEPENDS "${_wrap_header}" CXX_SCAN_FOR_MODULES OFF)
            endif()
        else()
            set_source_files_properties("${_wrap_cpp}" PROPERTIES OBJECT_DEPENDS "${_wrap_header}")
            list(APPEND _gentest_module_generated_sources "${_wrap_cpp}")
        endif()
    endforeach()

    set_source_files_properties(${_gentest_wrapper_cpp} PROPERTIES GENERATED TRUE SKIP_UNITY_BUILD_INCLUSION ON)
    if(_gentest_extra_cpp)
        set_source_files_properties(${_gentest_extra_cpp} PROPERTIES GENERATED TRUE SKIP_UNITY_BUILD_INCLUSION ON)
    endif()
    set_source_files_properties(${_gentest_wrapper_headers} PROPERTIES GENERATED TRUE)
    if(_gentest_module_generated_sources)
        set_source_files_properties(${_gentest_module_generated_sources} PROPERTIES CXX_SCAN_FOR_MODULES ON)
    endif()
    # Apply original source properties after wrapper defaults so explicit
    # per-source scan overrides are preserved for generated wrappers.
    _gentest_copy_source_properties_to_wrappers(
        TU_SOURCE_ENTRIES ${GENTEST_TU_SOURCE_ENTRIES}
        TUS ${GENTEST_TUS}
        WRAPPER_CPP ${_gentest_wrapper_cpp}
        EXTRA_CPP ${_gentest_registration_cpp})

    set(${GENTEST_OUT_OUTPUT_DIR} "${_gentest_output_dir}" PARENT_SCOPE)
    set(${GENTEST_OUT_WRAPPER_CPP} "${_gentest_wrapper_cpp}" PARENT_SCOPE)
    set(${GENTEST_OUT_WRAPPER_HEADERS} "${_gentest_wrapper_headers}" PARENT_SCOPE)
    set(${GENTEST_OUT_EXTRA_CPP} "${_gentest_extra_cpp}" PARENT_SCOPE)
    set(${GENTEST_OUT_ARTIFACT_MANIFEST} "${_gentest_output_dir}/${GENTEST_TARGET_ID}.artifact_manifest.json" PARENT_SCOPE)
    set(${GENTEST_OUT_COMPILE_CONTEXT_IDS} "${_gentest_compile_context_ids}" PARENT_SCOPE)
    set(${GENTEST_OUT_ARTIFACT_OWNER_SOURCES} "${_gentest_artifact_owner_sources}" PARENT_SCOPE)
endfunction()

function(_gentest_prepare_module_registration_mode)
    set(one_value_args TARGET TARGET_ID OUTPUT_DIR OUT_OUTPUT_DIR OUT_REGISTRATION_CPP OUT_WRAPPER_HEADERS OUT_MANIFEST OUT_COMPILE_CONTEXT_IDS)
    set(multi_value_args TUS TU_SOURCE_ENTRIES NEEDS_MODULE_SCAN)
    cmake_parse_arguments(GENTEST "" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(GENTEST_OUTPUT_DIR)
        set(_gentest_output_dir "${GENTEST_OUTPUT_DIR}")
    else()
        set(_gentest_output_dir "${CMAKE_CURRENT_BINARY_DIR}/gentest/${GENTEST_TARGET_ID}")
    endif()

    if("${_gentest_output_dir}" MATCHES "\\$<")
        message(FATAL_ERROR
            "gentest_attach_codegen(${GENTEST_TARGET}): OUTPUT_DIR contains generator expressions, which is not supported in "
            "MODULE_REGISTRATION mode. Pass a concrete OUTPUT_DIR.")
    endif()

    _gentest_normalize_path_and_key("${_gentest_output_dir}" "${CMAKE_CURRENT_BINARY_DIR}" _gentest_outdir_abs _gentest_outdir_key)
    set(_gentest_output_dir "${_gentest_outdir_abs}")

    _gentest_reserve_unique_owner("GENTEST_CODEGEN_OUTDIR_OWNER" "${_gentest_outdir_key}" "${GENTEST_TARGET}" _gentest_prev_owner)
    if(_gentest_prev_owner AND NOT _gentest_prev_owner STREQUAL "${GENTEST_TARGET}")
        message(FATAL_ERROR
            "gentest_attach_codegen(${GENTEST_TARGET}): OUTPUT_DIR '${_gentest_outdir_abs}' is already used by '${_gentest_prev_owner}'. "
            "Each target should have a unique OUTPUT_DIR to avoid generated file clobbering.")
    endif()

    set(_gentest_wrapper_headers "")
    set(_gentest_registration_cpp "")
    set(_gentest_compile_context_ids "")
    list(LENGTH GENTEST_TUS _gentest_tu_count)
    math(EXPR _gentest_last_tu "${_gentest_tu_count} - 1")
    foreach(_gentest_idx RANGE 0 ${_gentest_last_tu})
        list(GET GENTEST_TUS ${_gentest_idx} _tu)
        get_filename_component(_stem "${_tu}" NAME_WE)
        string(REGEX REPLACE "[^A-Za-z0-9_]" "_" _stem "${_stem}")
        if(_stem STREQUAL "")
            set(_stem "tu")
        endif()
        _gentest_shorten_generated_stem("${_stem}" _stem)
        set(_idx_str "${_gentest_idx}")
        string(LENGTH "${_idx_str}" _idx_len)
        if(_idx_len LESS 4)
            math(EXPR _pad "4 - ${_idx_len}")
            string(REPEAT "0" ${_pad} _zeros)
            set(_idx_str "${_zeros}${_idx_str}")
        endif()
        list(APPEND _gentest_wrapper_headers "${_gentest_output_dir}/tu_${_idx_str}_${_stem}.gentest.h")
        _gentest_make_module_registration_output_path("${_gentest_output_dir}" "${_tu}" ${_gentest_idx} _gentest_reg_cpp)
        list(APPEND _gentest_registration_cpp "${_gentest_reg_cpp}")
        list(APPEND _gentest_compile_context_ids "${GENTEST_TARGET_ID}:${_tu}")
    endforeach()

    file(MAKE_DIRECTORY "${_gentest_output_dir}")

    set_source_files_properties(${_gentest_registration_cpp} PROPERTIES
        GENERATED TRUE
        SKIP_UNITY_BUILD_INCLUSION ON
        CXX_SCAN_FOR_MODULES ON)
    set_source_files_properties(${_gentest_wrapper_headers} PROPERTIES GENERATED TRUE)
    _gentest_copy_source_properties_to_wrappers(
        TU_SOURCE_ENTRIES ${GENTEST_TU_SOURCE_ENTRIES}
        TUS ${GENTEST_TUS}
        WRAPPER_CPP ${_gentest_registration_cpp})
    set_source_files_properties(${_gentest_registration_cpp} PROPERTIES CXX_SCAN_FOR_MODULES ON)

    set(${GENTEST_OUT_OUTPUT_DIR} "${_gentest_output_dir}" PARENT_SCOPE)
    set(${GENTEST_OUT_REGISTRATION_CPP} "${_gentest_registration_cpp}" PARENT_SCOPE)
    set(${GENTEST_OUT_WRAPPER_HEADERS} "${_gentest_wrapper_headers}" PARENT_SCOPE)
    set(${GENTEST_OUT_MANIFEST} "${_gentest_output_dir}/${GENTEST_TARGET_ID}.artifact_manifest.json" PARENT_SCOPE)
    set(${GENTEST_OUT_COMPILE_CONTEXT_IDS} "${_gentest_compile_context_ids}" PARENT_SCOPE)
endfunction()

function(_gentest_append_artifact_manifest_validation_values args_var option)
    set(_gentest_args "${${args_var}}")
    foreach(_gentest_value IN LISTS ARGN)
        list(APPEND _gentest_args "${option}" "${_gentest_value}")
    endforeach()
    set(${args_var} "${_gentest_args}" PARENT_SCOPE)
endfunction()

function(_gentest_make_artifact_manifest_validation_args)
    set(one_value_args
        MANIFEST STAMP INCLUDE_DIR DEPFILE TARGET_ATTACHMENT ARTIFACT_ROLE COMPILE_AS REQUIRES_MODULE_SCAN
        INCLUDES_OWNER_SOURCE REPLACES_OWNER_SOURCE COMPDB OUT_ARGS)
    set(multi_value_args
        SOURCES SOURCE_KINDS REGISTRATION_OUTPUTS HEADERS COMPILE_CONTEXT_IDS OWNER_SOURCES SOURCE_REGISTRATION_OUTPUTS
        SCAN_CONTEXT_ARGS)
    cmake_parse_arguments(GENTEST "" "${one_value_args}" "${multi_value_args}" ${ARGN})

    set(_gentest_args
        validate-artifact-manifest
        --manifest "${GENTEST_MANIFEST}"
        --stamp "${GENTEST_STAMP}"
        --expected-include-dir "${GENTEST_INCLUDE_DIR}"
        --expected-depfile "${GENTEST_DEPFILE}"
        --expected-target-attachment "${GENTEST_TARGET_ATTACHMENT}"
        --expected-artifact-role "${GENTEST_ARTIFACT_ROLE}"
        --expected-compile-as "${GENTEST_COMPILE_AS}"
        --expected-requires-module-scan "${GENTEST_REQUIRES_MODULE_SCAN}")
    if(GENTEST_COMPDB)
        list(APPEND _gentest_args --compdb "${GENTEST_COMPDB}")
    endif()
    if(DEFINED GENTEST_INCLUDES_OWNER_SOURCE)
        list(APPEND _gentest_args --expected-includes-owner-source "${GENTEST_INCLUDES_OWNER_SOURCE}")
    endif()
    if(DEFINED GENTEST_REPLACES_OWNER_SOURCE)
        list(APPEND _gentest_args --expected-replaces-owner-source "${GENTEST_REPLACES_OWNER_SOURCE}")
    endif()
    _gentest_append_artifact_manifest_validation_values(_gentest_args "--expected-source" ${GENTEST_SOURCES})
    _gentest_append_artifact_manifest_validation_values(_gentest_args "--expected-source-kind" ${GENTEST_SOURCE_KINDS})
    _gentest_append_artifact_manifest_validation_values(_gentest_args "--expected-registration-output" ${GENTEST_REGISTRATION_OUTPUTS})
    _gentest_append_artifact_manifest_validation_values(_gentest_args "--expected-header" ${GENTEST_HEADERS})
    _gentest_append_artifact_manifest_validation_values(_gentest_args "--expected-compile-context-id" ${GENTEST_COMPILE_CONTEXT_IDS})
    if(GENTEST_OWNER_SOURCES)
        _gentest_append_artifact_manifest_validation_values(_gentest_args "--expected-owner-source" ${GENTEST_OWNER_SOURCES})
    endif()
    if(GENTEST_SOURCE_REGISTRATION_OUTPUTS)
        _gentest_append_artifact_manifest_validation_values(_gentest_args "--expected-source-registration-output"
            ${GENTEST_SOURCE_REGISTRATION_OUTPUTS})
    endif()
    if(GENTEST_SCAN_CONTEXT_ARGS)
        list(APPEND _gentest_args ${GENTEST_SCAN_CONTEXT_ARGS})
    endif()

    set(${GENTEST_OUT_ARGS} "${_gentest_args}" PARENT_SCOPE)
endfunction()

function(_gentest_add_artifact_manifest_validation_command)
    set(one_value_args STAMP COMMENT)
    set(multi_value_args COMMAND_LAUNCHER VALIDATION_ARGS DEPENDS)
    cmake_parse_arguments(GENTEST "" "${one_value_args}" "${multi_value_args}" ${ARGN})

    add_custom_command(
        OUTPUT "${GENTEST_STAMP}"
        COMMAND ${GENTEST_COMMAND_LAUNCHER} ${GENTEST_VALIDATION_ARGS}
        COMMAND_EXPAND_LISTS
        DEPENDS ${GENTEST_DEPENDS}
        COMMENT "${GENTEST_COMMENT}"
        VERBATIM)
endfunction()

function(_gentest_attach_manifest_codegen)
    set(one_value_args TARGET OUTPUT)
    cmake_parse_arguments(GENTEST "" "${one_value_args}" "" ${ARGN})
    target_sources(${GENTEST_TARGET} PRIVATE ${GENTEST_OUTPUT})
endfunction()

function(_gentest_attach_tu_wrapper_sources)
    set(one_value_args TARGET TARGET_ID)
    set(multi_value_args REPLACED_TUS REPLACED_SOURCE_ENTRIES WRAPPER_CPP MODULE_NAMES EXTRA_CPP CODEGEN_OUTPUTS)
    cmake_parse_arguments(GENTEST "" "${one_value_args}" "${multi_value_args}" ${ARGN})

    get_target_property(_gentest_target_sources ${GENTEST_TARGET} SOURCES)
    if(NOT _gentest_target_sources)
        set(_gentest_target_sources "")
    endif()

    set(_gentest_wrap_keys "")
    set(_gentest_module_source_entries "")
    set(_gentest_module_tus "")
    set(_gentest_module_wrappers "")
    set(_gentest_nonmodule_wrappers "")
    list(LENGTH GENTEST_REPLACED_TUS _gentest_replaced_count)
    math(EXPR _gentest_last_replaced "${_gentest_replaced_count} - 1")
    foreach(_idx RANGE 0 ${_gentest_last_replaced})
        list(GET GENTEST_REPLACED_TUS ${_idx} _tu)
        list(GET GENTEST_REPLACED_SOURCE_ENTRIES ${_idx} _src_entry)
        list(GET GENTEST_WRAPPER_CPP ${_idx} _wrap_cpp)
        list(GET GENTEST_MODULE_NAMES ${_idx} _module_name)
        _gentest_normalize_path_and_key("${_tu}" "${CMAKE_CURRENT_SOURCE_DIR}" _abs _key)
        list(APPEND _gentest_wrap_keys "${_key}")
        if(_module_name STREQUAL "__gentest_no_module__")
            list(APPEND _gentest_nonmodule_wrappers "${_wrap_cpp}")
        else()
            list(APPEND _gentest_module_source_entries "${_src_entry}")
            list(APPEND _gentest_module_tus "${_tu}")
            list(APPEND _gentest_module_wrappers "${_wrap_cpp}")
        endif()
    endforeach()

    set(_gentest_new_sources "")
    foreach(_src IN LISTS _gentest_target_sources)
        if("${_src}" MATCHES "\\$<")
            list(APPEND _gentest_new_sources "${_src}")
            continue()
        endif()
        list(FIND GENTEST_REPLACED_SOURCE_ENTRIES "${_src}" _src_entry_idx)
        if(NOT _src_entry_idx EQUAL -1)
            continue()
        endif()
        _gentest_normalize_path_and_key("${_src}" "${CMAKE_CURRENT_SOURCE_DIR}" _abs _key)
        list(FIND _gentest_wrap_keys "${_key}" _found)
        if(_found EQUAL -1)
            list(APPEND _gentest_new_sources "${_src}")
        endif()
    endforeach()

    if(_gentest_nonmodule_wrappers)
        list(APPEND _gentest_new_sources ${_gentest_nonmodule_wrappers})
    endif()
    set_property(TARGET ${GENTEST_TARGET} PROPERTY GENTEST_CODEGEN_NONMODULE_WRAPPERS "${_gentest_nonmodule_wrappers}")
    if(GENTEST_EXTRA_CPP)
        list(APPEND _gentest_new_sources ${GENTEST_EXTRA_CPP})
    endif()

    get_target_property(_gentest_module_sets ${GENTEST_TARGET} CXX_MODULE_SETS)
    set(_gentest_module_keys_in_sets "")
    if(NOT _gentest_module_sets STREQUAL "NOTFOUND")
        foreach(_gentest_module_set IN LISTS _gentest_module_sets)
            get_target_property(_gentest_module_files ${GENTEST_TARGET} CXX_MODULE_SET_${_gentest_module_set})
            if(_gentest_module_files STREQUAL "NOTFOUND")
                continue()
            endif()
            get_target_property(_gentest_module_dirs ${GENTEST_TARGET} CXX_MODULE_DIRS_${_gentest_module_set})
            if(_gentest_module_dirs STREQUAL "NOTFOUND")
                set(_gentest_module_dirs "")
            endif()
            set(_gentest_new_module_files "")
            foreach(_gentest_module_file IN LISTS _gentest_module_files)
                if("${_gentest_module_file}" MATCHES "\\$<")
                    list(APPEND _gentest_new_module_files "${_gentest_module_file}")
                    continue()
                endif()

                set(_gentest_replaced FALSE)
                list(LENGTH _gentest_module_wrappers _gentest_module_wrapper_count)
                if(_gentest_module_wrapper_count GREATER 0)
                    math(EXPR _gentest_last_module_wrapper "${_gentest_module_wrapper_count} - 1")
                    foreach(_idx RANGE 0 ${_gentest_last_module_wrapper})
                        list(GET _gentest_module_source_entries ${_idx} _module_src_entry)
                        list(GET _gentest_module_tus ${_idx} _module_tu)
                        list(GET _gentest_module_wrappers ${_idx} _module_wrap_cpp)
                        if(_gentest_module_file STREQUAL "${_module_src_entry}")
                            list(APPEND _gentest_new_module_files "${_module_wrap_cpp}")
                            _gentest_normalize_path_and_key("${_module_tu}" "${CMAKE_CURRENT_SOURCE_DIR}" _abs _key)
                            list(APPEND _gentest_module_keys_in_sets "${_key}")
                            set(_gentest_replaced TRUE)
                            break()
                        endif()
                        _gentest_normalize_path_and_key("${_gentest_module_file}" "${CMAKE_CURRENT_SOURCE_DIR}" _module_file_abs _module_file_key)
                        _gentest_normalize_path_and_key("${_module_tu}" "${CMAKE_CURRENT_SOURCE_DIR}" _module_tu_abs _module_tu_key)
                        if(_module_file_key STREQUAL _module_tu_key)
                            list(APPEND _gentest_new_module_files "${_module_wrap_cpp}")
                            list(APPEND _gentest_module_keys_in_sets "${_module_tu_key}")
                            set(_gentest_replaced TRUE)
                            break()
                        endif()
                    endforeach()
                endif()

                if(NOT _gentest_replaced)
                    list(APPEND _gentest_new_module_files "${_gentest_module_file}")
                endif()
            endforeach()

            set_property(TARGET ${GENTEST_TARGET} PROPERTY CXX_MODULE_SET_${_gentest_module_set} "${_gentest_new_module_files}")
            foreach(_gentest_module_wrapper IN LISTS _gentest_module_wrappers)
                get_filename_component(_gentest_module_wrapper_dir "${_gentest_module_wrapper}" DIRECTORY)
                list(APPEND _gentest_module_dirs "${_gentest_module_wrapper_dir}")
            endforeach()
            list(REMOVE_DUPLICATES _gentest_module_dirs)
            set_property(TARGET ${GENTEST_TARGET} PROPERTY CXX_MODULE_DIRS_${_gentest_module_set} "${_gentest_module_dirs}")
        endforeach()
    endif()

    if(_gentest_module_wrappers)
        set(_gentest_generated_module_wrappers "")
        list(LENGTH _gentest_module_wrappers _gentest_module_wrapper_count)
        math(EXPR _gentest_last_module_wrapper "${_gentest_module_wrapper_count} - 1")
        foreach(_idx RANGE 0 ${_gentest_last_module_wrapper})
            list(GET _gentest_module_tus ${_idx} _module_tu)
            list(GET _gentest_module_wrappers ${_idx} _module_wrap_cpp)
            _gentest_normalize_path_and_key("${_module_tu}" "${CMAKE_CURRENT_SOURCE_DIR}" _module_tu_abs _module_tu_key)
            list(FIND _gentest_module_keys_in_sets "${_module_tu_key}" _module_in_set_idx)
            if(_module_in_set_idx EQUAL -1)
                list(APPEND _gentest_generated_module_wrappers "${_module_wrap_cpp}")
            endif()
        endforeach()
        if(_gentest_generated_module_wrappers)
            set(_gentest_generated_module_dirs "")
            foreach(_gentest_module_wrapper IN LISTS _gentest_generated_module_wrappers)
                get_filename_component(_gentest_module_wrapper_dir "${_gentest_module_wrapper}" DIRECTORY)
                list(APPEND _gentest_generated_module_dirs "${_gentest_module_wrapper_dir}")
            endforeach()
            list(REMOVE_DUPLICATES _gentest_generated_module_dirs)
            target_sources(${GENTEST_TARGET}
                PRIVATE
                    FILE_SET gentest_codegen_modules_${GENTEST_TARGET_ID}
                    TYPE CXX_MODULES
                    BASE_DIRS ${_gentest_generated_module_dirs}
                    FILES ${_gentest_generated_module_wrappers})
        endif()
    endif()

    set_property(TARGET ${GENTEST_TARGET} PROPERTY SOURCES "${_gentest_new_sources}")

    add_custom_target(gentest_codegen_${GENTEST_TARGET_ID} DEPENDS ${GENTEST_CODEGEN_OUTPUTS})
    set_property(TARGET ${GENTEST_TARGET} PROPERTY GENTEST_CODEGEN_DEP_TARGET gentest_codegen_${GENTEST_TARGET_ID})
    if(TARGET gentest_codegen_all)
        add_dependencies(gentest_codegen_all gentest_codegen_${GENTEST_TARGET_ID})
    endif()
    add_dependencies(${GENTEST_TARGET} gentest_codegen_${GENTEST_TARGET_ID})
endfunction()

function(_gentest_attach_module_registration_sources)
    set(one_value_args TARGET TARGET_ID)
    set(multi_value_args REGISTRATION_CPP CODEGEN_OUTPUTS)
    cmake_parse_arguments(GENTEST "" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(GENTEST_REGISTRATION_CPP)
        target_sources(${GENTEST_TARGET} PRIVATE ${GENTEST_REGISTRATION_CPP})
        set_source_files_properties(${GENTEST_REGISTRATION_CPP} TARGET_DIRECTORY ${GENTEST_TARGET} PROPERTIES CXX_SCAN_FOR_MODULES ON)
    endif()

    add_custom_target(gentest_codegen_${GENTEST_TARGET_ID} DEPENDS ${GENTEST_CODEGEN_OUTPUTS})
    set_property(TARGET ${GENTEST_TARGET} PROPERTY GENTEST_CODEGEN_DEP_TARGET gentest_codegen_${GENTEST_TARGET_ID})
    if(TARGET gentest_codegen_all)
        add_dependencies(gentest_codegen_all gentest_codegen_${GENTEST_TARGET_ID})
    endif()
    add_dependencies(${GENTEST_TARGET} gentest_codegen_${GENTEST_TARGET_ID})
endfunction()

function(_gentest_collect_scan_include_dirs target source out_dirs)
    set(_gentest_dirs "")

    foreach(_gentest_prop IN ITEMS INCLUDE_DIRECTORIES SYSTEM_INCLUDE_DIRECTORIES)
        get_target_property(_gentest_prop_values ${target} ${_gentest_prop})
        if(NOT _gentest_prop_values STREQUAL "NOTFOUND" AND NOT _gentest_prop_values MATCHES "-NOTFOUND$")
            foreach(_gentest_dir IN LISTS _gentest_prop_values)
                if("${_gentest_dir}" MATCHES "\\$<")
                    continue()
                endif()
                get_filename_component(_gentest_abs_dir "${_gentest_dir}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
                list(APPEND _gentest_dirs "${_gentest_abs_dir}")
            endforeach()
        endif()
    endforeach()

    get_source_file_property(_gentest_source_include_dirs "${source}" INCLUDE_DIRECTORIES)
    if(NOT _gentest_source_include_dirs STREQUAL "NOTFOUND" AND NOT _gentest_source_include_dirs MATCHES "-NOTFOUND$")
        foreach(_gentest_dir IN LISTS _gentest_source_include_dirs)
            if("${_gentest_dir}" MATCHES "\\$<")
                continue()
            endif()
            get_filename_component(_gentest_abs_dir "${_gentest_dir}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
            list(APPEND _gentest_dirs "${_gentest_abs_dir}")
        endforeach()
    endif()

    set(_gentest_expect_include_dir FALSE)
    foreach(_gentest_arg IN LISTS ARGN)
        if(_gentest_expect_include_dir)
            set(_gentest_expect_include_dir FALSE)
            if(NOT "${_gentest_arg}" MATCHES "\\$<")
                get_filename_component(_gentest_abs_dir "${_gentest_arg}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
                list(APPEND _gentest_dirs "${_gentest_abs_dir}")
            endif()
            continue()
        endif()

        if(_gentest_arg STREQUAL "-I" OR _gentest_arg STREQUAL "-isystem" OR _gentest_arg STREQUAL "-iquote" OR
           _gentest_arg STREQUAL "-idirafter" OR _gentest_arg STREQUAL "/I")
            set(_gentest_expect_include_dir TRUE)
            continue()
        endif()

        set(_gentest_joined_prefixes "-I" "-isystem" "-iquote" "-idirafter" "/I")
        foreach(_gentest_prefix IN LISTS _gentest_joined_prefixes)
            string(LENGTH "${_gentest_prefix}" _gentest_prefix_len)
            string(SUBSTRING "${_gentest_arg}" 0 ${_gentest_prefix_len} _gentest_arg_prefix)
            if(_gentest_arg_prefix STREQUAL _gentest_prefix)
                string(SUBSTRING "${_gentest_arg}" ${_gentest_prefix_len} -1 _gentest_dir)
                if(NOT "${_gentest_dir}" STREQUAL "" AND NOT "${_gentest_dir}" MATCHES "\\$<")
                    get_filename_component(_gentest_abs_dir "${_gentest_dir}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
                    list(APPEND _gentest_dirs "${_gentest_abs_dir}")
                endif()
                break()
            endif()
        endforeach()
    endforeach()

    list(REMOVE_DUPLICATES _gentest_dirs)
    set(${out_dirs} "${_gentest_dirs}" PARENT_SCOPE)
endfunction()

function(_gentest_collect_scan_include_dirs_from_sequence base_dir out_dirs)
    set(_gentest_dirs "")
    set(_gentest_expect_include_dir FALSE)
    foreach(_gentest_arg IN LISTS ARGN)
        if(_gentest_expect_include_dir)
            set(_gentest_expect_include_dir FALSE)
            if(NOT "${_gentest_arg}" MATCHES "\\$<")
                get_filename_component(_gentest_abs_dir "${_gentest_arg}" ABSOLUTE BASE_DIR "${base_dir}")
                list(APPEND _gentest_dirs "${_gentest_abs_dir}")
            endif()
            continue()
        endif()

        if(_gentest_arg STREQUAL "-I" OR _gentest_arg STREQUAL "-isystem" OR _gentest_arg STREQUAL "-iquote" OR
           _gentest_arg STREQUAL "-idirafter" OR _gentest_arg STREQUAL "/I")
            set(_gentest_expect_include_dir TRUE)
            continue()
        endif()

        set(_gentest_joined_prefixes "-I" "-isystem" "-iquote" "-idirafter" "/I")
        foreach(_gentest_prefix IN LISTS _gentest_joined_prefixes)
            string(LENGTH "${_gentest_prefix}" _gentest_prefix_len)
            string(SUBSTRING "${_gentest_arg}" 0 ${_gentest_prefix_len} _gentest_arg_prefix)
            if(_gentest_arg_prefix STREQUAL _gentest_prefix)
                string(SUBSTRING "${_gentest_arg}" ${_gentest_prefix_len} -1 _gentest_dir)
                if(NOT "${_gentest_dir}" STREQUAL "" AND NOT "${_gentest_dir}" MATCHES "\\$<")
                    get_filename_component(_gentest_abs_dir "${_gentest_dir}" ABSOLUTE BASE_DIR "${base_dir}")
                    list(APPEND _gentest_dirs "${_gentest_abs_dir}")
                endif()
                break()
            endif()
        endforeach()
    endforeach()

    list(REMOVE_DUPLICATES _gentest_dirs)
    set(${out_dirs} "${_gentest_dirs}" PARENT_SCOPE)
endfunction()

function(_gentest_collect_scan_macro_args_from_sequence out_args out_has_genex)
    set(_gentest_args "")
    set(_gentest_has_genex FALSE)
    set(_gentest_expect_define FALSE)
    set(_gentest_expect_undef FALSE)
    foreach(_gentest_arg IN LISTS ARGN)
        if(_gentest_expect_define)
            if("${_gentest_arg}" MATCHES "\\$<")
                set(_gentest_has_genex TRUE)
                set(_gentest_expect_define FALSE)
                continue()
            endif()
            list(APPEND _gentest_args -D "${_gentest_arg}")
            set(_gentest_expect_define FALSE)
            continue()
        endif()
        if(_gentest_expect_undef)
            if("${_gentest_arg}" MATCHES "\\$<")
                set(_gentest_has_genex TRUE)
                set(_gentest_expect_undef FALSE)
                continue()
            endif()
            list(APPEND _gentest_args -U "${_gentest_arg}")
            set(_gentest_expect_undef FALSE)
            continue()
        endif()

        if(_gentest_arg STREQUAL "-D" OR _gentest_arg STREQUAL "/D")
            set(_gentest_expect_define TRUE)
            continue()
        endif()
        if(_gentest_arg STREQUAL "-U" OR _gentest_arg STREQUAL "/U")
            set(_gentest_expect_undef TRUE)
            continue()
        endif()
        if(_gentest_arg MATCHES "^[-/]D.+$")
            if("${_gentest_arg}" MATCHES "\\$<")
                set(_gentest_has_genex TRUE)
                continue()
            endif()
            list(APPEND _gentest_args "${_gentest_arg}")
            continue()
        endif()
        if(_gentest_arg MATCHES "^[-/]U.+$")
            if("${_gentest_arg}" MATCHES "\\$<")
                set(_gentest_has_genex TRUE)
                continue()
            endif()
            list(APPEND _gentest_args "${_gentest_arg}")
        endif()
    endforeach()

    set(${out_args} "${_gentest_args}" PARENT_SCOPE)
    set(${out_has_genex} "${_gentest_has_genex}" PARENT_SCOPE)
endfunction()

function(_gentest_collect_scan_macro_args target source out_args out_has_genex)
    set(_gentest_definition_props COMPILE_DEFINITIONS)
    if(NOT CMAKE_CONFIGURATION_TYPES AND CMAKE_BUILD_TYPE)
        set(_gentest_configs ${CMAKE_BUILD_TYPE})
    else()
        set(_gentest_configs "")
    endif()
    foreach(_cfg IN LISTS _gentest_configs)
        string(TOUPPER "${_cfg}" _cfg_upper)
        list(APPEND _gentest_definition_props "COMPILE_DEFINITIONS_${_cfg_upper}")
    endforeach()
    list(REMOVE_DUPLICATES _gentest_definition_props)

    set(_gentest_args "")
    set(_gentest_has_genex FALSE)
    foreach(_gentest_prop IN LISTS _gentest_definition_props)
        get_target_property(_gentest_target_defs ${target} ${_gentest_prop})
        if(NOT _gentest_target_defs STREQUAL "NOTFOUND" AND NOT _gentest_target_defs MATCHES "-NOTFOUND$")
            foreach(_gentest_def IN LISTS _gentest_target_defs)
                if("${_gentest_def}" MATCHES "\\$<")
                    continue()
                endif()
                list(APPEND _gentest_args "-D${_gentest_def}")
            endforeach()
        endif()

        get_source_file_property(_gentest_source_defs "${source}" ${_gentest_prop})
        if(NOT _gentest_source_defs STREQUAL "NOTFOUND" AND NOT _gentest_source_defs MATCHES "-NOTFOUND$")
            foreach(_gentest_def IN LISTS _gentest_source_defs)
                if("${_gentest_def}" MATCHES "\\$<")
                    continue()
                endif()
                list(APPEND _gentest_args "-D${_gentest_def}")
            endforeach()
        endif()
    endforeach()

    _gentest_collect_scan_macro_args_from_sequence(_gentest_argn_args _gentest_argn_has_genex ${ARGN})
    list(APPEND _gentest_args ${_gentest_argn_args})
    if(_gentest_argn_has_genex)
        set(_gentest_has_genex TRUE)
    endif()
    set(${out_args} "${_gentest_args}" PARENT_SCOPE)
    set(${out_has_genex} "${_gentest_has_genex}" PARENT_SCOPE)
endfunction()

function(_gentest_collect_codegen_dep_targets_from_link_graph target out_targets)
    set(_gentest_result "")
    set(_gentest_explicit_mock_targets "")
    set(_gentest_queue "${target}")
    set(_gentest_seen "")

    while(_gentest_queue)
        list(POP_FRONT _gentest_queue _gentest_current)
        if(NOT TARGET "${_gentest_current}")
            continue()
        endif()

        get_target_property(_gentest_alias_target "${_gentest_current}" ALIASED_TARGET)
        if(_gentest_alias_target AND NOT _gentest_alias_target STREQUAL "_gentest_alias_target-NOTFOUND")
            set(_gentest_current "${_gentest_alias_target}")
        endif()

        list(FIND _gentest_seen "${_gentest_current}" _gentest_seen_index)
        if(NOT _gentest_seen_index EQUAL -1)
            continue()
        endif()
        list(APPEND _gentest_seen "${_gentest_current}")

        if(NOT _gentest_current STREQUAL "${target}")
            get_target_property(_gentest_is_explicit_mock "${_gentest_current}" GENTEST_EXPLICIT_MOCK_TARGET)
            if(_gentest_is_explicit_mock)
                get_target_property(_gentest_explicit_codegen_dep "${_gentest_current}" GENTEST_CODEGEN_DEP_TARGET)
                if(_gentest_explicit_codegen_dep AND NOT _gentest_explicit_codegen_dep MATCHES "-NOTFOUND$")
                    list(APPEND _gentest_result "${_gentest_explicit_codegen_dep}")
                endif()
                list(APPEND _gentest_result "${_gentest_current}")
                list(APPEND _gentest_explicit_mock_targets "${_gentest_current}")
            endif()
        endif()

        foreach(_gentest_link_prop IN ITEMS LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
            get_target_property(_gentest_link_deps "${_gentest_current}" "${_gentest_link_prop}")
            if(NOT _gentest_link_deps OR _gentest_link_deps MATCHES "-NOTFOUND$")
                continue()
            endif()
            foreach(_gentest_link_dep IN LISTS _gentest_link_deps)
                if("${_gentest_link_dep}" STREQUAL "" OR "${_gentest_link_dep}" MATCHES "\\$<")
                    continue()
                endif()
                if(TARGET "${_gentest_link_dep}")
                    list(APPEND _gentest_queue "${_gentest_link_dep}")
                endif()
            endforeach()
        endforeach()
    endwhile()

    list(REMOVE_DUPLICATES _gentest_result)
    set(${out_targets} "${_gentest_result}" PARENT_SCOPE)
    if(ARGC GREATER 2)
        list(REMOVE_DUPLICATES _gentest_explicit_mock_targets)
        set(${ARGV2} "${_gentest_explicit_mock_targets}" PARENT_SCOPE)
    endif()
endfunction()

function(_gentest_target_has_module_metadata target out_has_modules)
    set(_gentest_has_modules FALSE)
    if(TARGET "${target}")
        get_target_property(_gentest_scan_for_modules "${target}" CXX_SCAN_FOR_MODULES)
        if(_gentest_scan_for_modules AND NOT _gentest_scan_for_modules MATCHES "-NOTFOUND$")
            set(_gentest_has_modules TRUE)
        endif()
        foreach(_gentest_module_prop IN ITEMS
                CXX_MODULE_SETS
                INTERFACE_CXX_MODULE_SETS
                GENTEST_EXPLICIT_MOCK_MODULE_BUILD_SOURCES
                GENTEST_EXPLICIT_MOCK_MODULE_REL_SOURCES)
            if(_gentest_has_modules)
                break()
            endif()
            get_target_property(_gentest_module_values "${target}" "${_gentest_module_prop}")
            if(_gentest_module_values AND NOT _gentest_module_values MATCHES "-NOTFOUND$")
                set(_gentest_has_modules TRUE)
                break()
            endif()
        endforeach()
    endif()
    set(${out_has_modules} "${_gentest_has_modules}" PARENT_SCOPE)
endfunction()

function(_gentest_link_graph_has_explicit_mock_module_context target out_has_modules)
    set(_gentest_has_modules FALSE)
    set(_gentest_queue "${target}")
    set(_gentest_seen "")

    while(_gentest_queue)
        list(POP_FRONT _gentest_queue _gentest_current)
        if(NOT TARGET "${_gentest_current}")
            continue()
        endif()

        get_target_property(_gentest_alias_target "${_gentest_current}" ALIASED_TARGET)
        if(_gentest_alias_target AND NOT _gentest_alias_target MATCHES "-NOTFOUND$")
            set(_gentest_current "${_gentest_alias_target}")
        endif()

        list(FIND _gentest_seen "${_gentest_current}" _gentest_seen_index)
        if(NOT _gentest_seen_index EQUAL -1)
            continue()
        endif()
        list(APPEND _gentest_seen "${_gentest_current}")

        if(NOT _gentest_current STREQUAL "${target}")
            get_target_property(_gentest_is_explicit_mock "${_gentest_current}" GENTEST_EXPLICIT_MOCK_TARGET)
            if(_gentest_is_explicit_mock)
                _gentest_target_has_module_metadata("${_gentest_current}" _gentest_current_has_modules)
                if(_gentest_current_has_modules)
                    set(_gentest_has_modules TRUE)
                    break()
                endif()
            endif()
        endif()

        foreach(_gentest_link_prop IN ITEMS LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
            get_target_property(_gentest_link_deps "${_gentest_current}" "${_gentest_link_prop}")
            if(NOT _gentest_link_deps OR _gentest_link_deps MATCHES "-NOTFOUND$")
                continue()
            endif()
            foreach(_gentest_link_dep IN LISTS _gentest_link_deps)
                if("${_gentest_link_dep}" STREQUAL "" OR "${_gentest_link_dep}" MATCHES "\\$<")
                    continue()
                endif()
                if(TARGET "${_gentest_link_dep}")
                    list(APPEND _gentest_queue "${_gentest_link_dep}")
                endif()
            endforeach()
        endforeach()
    endwhile()

    set(${out_has_modules} "${_gentest_has_modules}" PARENT_SCOPE)
endfunction()

function(_gentest_enable_nonmodule_wrapper_scanning target)
    get_target_property(_gentest_nonmodule_wrappers "${target}" GENTEST_CODEGEN_NONMODULE_WRAPPERS)
    if(_gentest_nonmodule_wrappers AND NOT _gentest_nonmodule_wrappers MATCHES "-NOTFOUND$")
        set_source_files_properties(${_gentest_nonmodule_wrappers} TARGET_DIRECTORY ${target} PROPERTIES CXX_SCAN_FOR_MODULES ON)
    endif()
endfunction()

# Internal convenience helper for in-tree consumers that link explicit mock
# targets after gentest_attach_codegen() has already added the codegen target.
# The primary public contract remains: link explicit mock targets before
# gentest_attach_codegen().
function(gentest_link_mocks target)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "gentest_link_mocks(${target}): target does not exist")
    endif()

    set(_gentest_scope "PRIVATE")
    set(_gentest_mock_targets ${ARGN})
    if(ARGN)
        list(GET ARGN 0 _gentest_first_arg)
        if(_gentest_first_arg STREQUAL "PRIVATE" OR _gentest_first_arg STREQUAL "PUBLIC" OR _gentest_first_arg STREQUAL "INTERFACE")
            set(_gentest_scope "${_gentest_first_arg}")
            list(REMOVE_AT _gentest_mock_targets 0)
        endif()
    endif()

    if(NOT _gentest_mock_targets)
        message(FATAL_ERROR
            "gentest_link_mocks(${target}): provide at least one explicit mock target. "
            "Usage: gentest_link_mocks(<target> [PRIVATE|PUBLIC|INTERFACE] <mock-target>...)")
    endif()

    target_link_libraries(${target} ${_gentest_scope} ${_gentest_mock_targets})

    get_target_property(_gentest_consumer_codegen_dep "${target}" GENTEST_CODEGEN_DEP_TARGET)
    if(NOT _gentest_consumer_codegen_dep OR _gentest_consumer_codegen_dep MATCHES "-NOTFOUND$")
        return()
    endif()

    foreach(_gentest_mock_target IN LISTS _gentest_mock_targets)
        if(NOT TARGET "${_gentest_mock_target}")
            continue()
        endif()

        get_target_property(_gentest_mock_alias_target "${_gentest_mock_target}" ALIASED_TARGET)
        if(_gentest_mock_alias_target AND NOT _gentest_mock_alias_target STREQUAL "_gentest_mock_alias_target-NOTFOUND")
            set(_gentest_mock_target_actual "${_gentest_mock_alias_target}")
        else()
            set(_gentest_mock_target_actual "${_gentest_mock_target}")
        endif()

        get_target_property(_gentest_is_explicit_mock "${_gentest_mock_target_actual}" GENTEST_EXPLICIT_MOCK_TARGET)
        if(NOT _gentest_is_explicit_mock)
            continue()
        endif()

        _gentest_target_has_module_metadata("${_gentest_mock_target_actual}" _gentest_mock_has_module_metadata)
        if(_gentest_mock_has_module_metadata)
            _gentest_enable_nonmodule_wrapper_scanning(${target})
        endif()

        get_target_property(_gentest_mock_codegen_dep "${_gentest_mock_target_actual}" GENTEST_CODEGEN_DEP_TARGET)
        if(_gentest_mock_codegen_dep AND NOT _gentest_mock_codegen_dep MATCHES "-NOTFOUND$")
            add_dependencies(${_gentest_consumer_codegen_dep} ${_gentest_mock_codegen_dep})
        endif()
        add_dependencies(${_gentest_consumer_codegen_dep} ${_gentest_mock_target_actual})
        get_target_property(_gentest_mock_codegen_outputs "${_gentest_mock_target_actual}" GENTEST_CODEGEN_OUTPUTS)
        if(_gentest_mock_codegen_outputs AND NOT _gentest_mock_codegen_outputs MATCHES "-NOTFOUND$")
            _gentest_append_target_list_property(${target} GENTEST_CODEGEN_EXTRA_DEPENDS ${_gentest_mock_codegen_outputs})
        endif()
        list(APPEND _gentest_explicit_mock_targets_for_codegen "${_gentest_mock_target_actual}")
    endforeach()

    if(_gentest_explicit_mock_targets_for_codegen)
        _gentest_collect_external_module_source_mappings(_gentest_late_external_module_source_mappings
            ${_gentest_explicit_mock_targets_for_codegen})
        _gentest_append_external_module_source_args_property(${target} ${_gentest_late_external_module_source_mappings})
    endif()
endfunction()

function(gentest_attach_codegen target)
    set(options NO_INCLUDE_SOURCES STRICT_FIXTURE QUIET_CLANG MODULE_REGISTRATION)
    set(one_value_args OUTPUT OUTPUT_DIR ENTRY FILE_SET)
    set(multi_value_args SOURCES CLANG_ARGS DEPENDS)
    cmake_parse_arguments(GENTEST "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT GENTEST_ENTRY)
        set(GENTEST_ENTRY gentest::run_all_tests)
    endif()

    string(MAKE_C_IDENTIFIER "${target}" _gentest_target_id)
    get_target_property(_gentest_attach_discovers_mocks ${target} GENTEST_EXPLICIT_MOCK_TARGET)
    if(NOT _gentest_attach_discovers_mocks OR _gentest_attach_discovers_mocks MATCHES "-NOTFOUND$")
        set(_gentest_attach_discovers_mocks FALSE)
    endif()

    if(GENTEST_MODULE_REGISTRATION)
        if(GENTEST_OUTPUT)
            message(FATAL_ERROR
                "gentest_attach_codegen(${target}): MODULE_REGISTRATION cannot be combined with OUTPUT manifest mode.")
        endif()
        if(NOT GENTEST_FILE_SET)
            message(FATAL_ERROR
                "gentest_attach_codegen(${target}): MODULE_REGISTRATION requires FILE_SET <name>.")
        endif()
        if(GENTEST_SOURCES)
            message(FATAL_ERROR
                "gentest_attach_codegen(${target}): MODULE_REGISTRATION selects sources from FILE_SET; SOURCES is not supported.")
        endif()
    elseif(GENTEST_FILE_SET)
        message(FATAL_ERROR
            "gentest_attach_codegen(${target}): FILE_SET is only valid with MODULE_REGISTRATION.")
    endif()

    # Scan sources: explicit SOURCES preferred, otherwise pull from target and
    # any named module file sets attached to it. CMake only uses buildsystem
    # metadata for configure-time source shape; gentest_codegen owns source
    # parsing during the build.
    set(_gentest_scan_sources "${GENTEST_SOURCES}")
    set(_gentest_cxx_module_source_keys "")
    get_target_property(_gentest_module_sets ${target} CXX_MODULE_SETS)
    if(_gentest_module_sets AND NOT _gentest_module_sets MATCHES "-NOTFOUND$")
        foreach(_gentest_module_set IN LISTS _gentest_module_sets)
            get_target_property(_gentest_module_files ${target} CXX_MODULE_SET_${_gentest_module_set})
            if(NOT _gentest_module_files OR _gentest_module_files MATCHES "-NOTFOUND$")
                continue()
            endif()
            foreach(_gentest_module_file IN LISTS _gentest_module_files)
                if("${_gentest_module_file}" MATCHES "\\$<")
                    continue()
                endif()
                _gentest_normalize_path_and_key("${_gentest_module_file}" "${CMAKE_CURRENT_SOURCE_DIR}"
                    _gentest_module_file_abs _gentest_module_file_key)
                list(APPEND _gentest_cxx_module_source_keys "${_gentest_module_file_key}")
            endforeach()
        endforeach()
    endif()
    list(REMOVE_DUPLICATES _gentest_cxx_module_source_keys)

    if(GENTEST_MODULE_REGISTRATION)
        get_target_property(_gentest_scan_sources ${target} CXX_MODULE_SET_${GENTEST_FILE_SET})
        if(NOT _gentest_scan_sources OR _gentest_scan_sources MATCHES "-NOTFOUND$")
            message(FATAL_ERROR
                "gentest_attach_codegen(${target}): FILE_SET '${GENTEST_FILE_SET}' does not exist or has no CXX_MODULES sources.")
        endif()
    elseif(NOT _gentest_scan_sources)
        get_target_property(_gentest_scan_sources ${target} SOURCES)

        if(_gentest_module_sets AND NOT _gentest_module_sets MATCHES "-NOTFOUND$")
            foreach(_gentest_module_set IN LISTS _gentest_module_sets)
                get_target_property(_gentest_module_files ${target} CXX_MODULE_SET_${_gentest_module_set})
                if(_gentest_module_files AND NOT _gentest_module_files MATCHES "-NOTFOUND$")
                    list(APPEND _gentest_scan_sources ${_gentest_module_files})
                endif()
            endforeach()
        endif()
    endif()
    if(NOT _gentest_scan_sources)
        message(FATAL_ERROR "gentest_attach_codegen(${target}): SOURCES not provided and target has no SOURCES property")
    endif()

    _gentest_resolve_codegen_backend(
        TARGET ${target}
        OUT_CODEGEN_TARGET _gentest_codegen_target
        OUT_CODEGEN_EXECUTABLE _gentest_codegen_executable
        OUT_CODEGEN_BACKEND_KIND _gentest_codegen_backend_kind)
    _gentest_collect_common_codegen_clang_args(_gentest_source_inspection_clang_args)
    _gentest_collect_common_codegen_system_include_args(_gentest_source_inspection_system_include_args)

    # Select translation units and named module interface units (no generator expressions).
    set(_gentest_tus "")
    set(_gentest_tu_source_entries "")
    set(_gentest_module_names "")
    set(_gentest_manifest_validation_scan_context_args "")
    set(_gentest_skipped_genex_sources "")
    set(_gentest_seen_scan_source_keys "")
    foreach(_gentest_src IN LISTS _gentest_scan_sources)
        if("${_gentest_src}" MATCHES "\\$<")
            list(APPEND _gentest_skipped_genex_sources "${_gentest_src}")
            continue()
        endif()
        get_filename_component(_gentest_ext "${_gentest_src}" EXT)
        if(NOT _gentest_ext MATCHES "^\\.(cc|cpp|cxx|cppm|ccm|cxxm|ixx|mxx)$")
            continue()
        endif()
        _gentest_normalize_path_and_key("${_gentest_src}" "${CMAKE_CURRENT_SOURCE_DIR}" _gentest_src_abs _gentest_src_key)
        if(_gentest_src_key IN_LIST _gentest_seen_scan_source_keys)
            continue()
        endif()
        list(APPEND _gentest_seen_scan_source_keys "${_gentest_src_key}")

        if(GENTEST_MODULE_REGISTRATION)
            _gentest_collect_scan_include_dirs(${target} "${_gentest_src}" _gentest_scan_include_dirs
                ${_gentest_source_inspection_clang_args}
                ${_gentest_source_inspection_system_include_args})
            _gentest_collect_scan_macro_args(${target} "${_gentest_src}" _gentest_scan_macro_args _gentest_scan_macro_has_genex
                ${_gentest_source_inspection_clang_args})
            foreach(_gentest_scan_include_dir IN LISTS _gentest_scan_include_dirs)
                list(APPEND _gentest_manifest_validation_scan_context_args
                    --expected-source-scan-include-dir "${_gentest_src_abs}" "${_gentest_scan_include_dir}")
            endforeach()
            foreach(_gentest_scan_arg IN LISTS _gentest_scan_macro_args)
                list(APPEND _gentest_manifest_validation_scan_context_args
                    --expected-source-scan-arg "${_gentest_src_abs}" "${_gentest_scan_arg}")
            endforeach()
        endif()

        set(_gentest_module_name "")
        get_source_file_property(_gentest_declared_module_name "${_gentest_src}" GENTEST_MODULE_NAME)
        if(_gentest_declared_module_name STREQUAL "NOTFOUND")
            get_source_file_property(_gentest_declared_module_name "${_gentest_src_abs}" GENTEST_MODULE_NAME)
        endif()
        if(_gentest_declared_module_name STREQUAL "NOTFOUND")
            set(_gentest_declared_module_name "")
        endif()

        set(_gentest_is_module FALSE)
        if(GENTEST_MODULE_REGISTRATION)
            set(_gentest_is_module TRUE)
        elseif(NOT _gentest_declared_module_name STREQUAL "")
            set(_gentest_is_module TRUE)
            set(_gentest_module_name "${_gentest_declared_module_name}")
        elseif(_gentest_src_key IN_LIST _gentest_cxx_module_source_keys)
            set(_gentest_is_module TRUE)
        elseif(_gentest_ext MATCHES "^\\.(cppm|ccm|cxxm|ixx|mxx)$")
            set(_gentest_is_module TRUE)
        endif()
        list(APPEND _gentest_tu_source_entries "${_gentest_src}")
        list(APPEND _gentest_tus "${_gentest_src_abs}")
        if(_gentest_is_module)
            if(GENTEST_MODULE_REGISTRATION)
                list(APPEND _gentest_module_names "__gentest_module_registration__")
            else()
                if(_gentest_module_name STREQUAL "")
                    string(MD5 _gentest_module_key "${_gentest_src_key}")
                    set(_gentest_module_name "__gentest_module_${_gentest_module_key}")
                endif()
                list(APPEND _gentest_module_names "${_gentest_module_name}")
            endif()
        else()
            list(APPEND _gentest_module_names "__gentest_no_module__")
        endif()
    endforeach()

    if(_gentest_skipped_genex_sources)
        string(JOIN "', '" _gentest_skipped_genex_joined ${_gentest_skipped_genex_sources})
        message(FATAL_ERROR
            "gentest_attach_codegen(${target}): generator-expression SOURCES entries are not supported because they can be skipped by "
            "codegen. Pass concrete files via SOURCES=... instead. Offending entries: '${_gentest_skipped_genex_joined}'")
    endif()

    if(NOT _gentest_tus)
        message(FATAL_ERROR "gentest_attach_codegen(${target}): no C++ translation units or module units found to scan")
    endif()

    # Do not infer wrapper module scanning from source text: target compile
    # definitions can make imports active/inactive. CMake target metadata is
    # the supported signal for module-aware targets.
    _gentest_target_has_module_metadata(${target} _gentest_target_module_context)
    if(NOT _gentest_target_module_context)
        _gentest_link_graph_has_explicit_mock_module_context(${target} _gentest_target_module_context)
    endif()
    foreach(_gentest_module_name IN LISTS _gentest_module_names)
        if(NOT _gentest_module_name STREQUAL "__gentest_no_module__")
            set(_gentest_target_module_context TRUE)
            break()
        endif()
    endforeach()
    set(_gentest_needs_module_scan "")
    foreach(_gentest_module_name IN LISTS _gentest_module_names)
        if(_gentest_module_name STREQUAL "__gentest_no_module__")
            list(APPEND _gentest_needs_module_scan "${_gentest_target_module_context}")
        else()
            list(APPEND _gentest_needs_module_scan TRUE)
        endif()
    endforeach()

    # Mode selection:
    # - MODULE_REGISTRATION emits additive same-module implementation units.
    # - If OUTPUT is provided, emit a single manifest TU (legacy).
    # - Otherwise, emit a wrapper TU + header per translation unit and replace
    #   the target sources (gtest/catch/doctest-like workflow).
    set(_gentest_mode "tu")
    if(GENTEST_MODULE_REGISTRATION)
        set(_gentest_mode "module_registration")
    elseif(GENTEST_OUTPUT)
        set(_gentest_mode "manifest")
    endif()

    if((_gentest_mode STREQUAL "tu" OR _gentest_mode STREQUAL "module_registration") AND CMAKE_CONFIGURATION_TYPES)
        message(FATAL_ERROR
            "gentest_attach_codegen(${target}): ${_gentest_mode} mode is not supported with multi-config generators. "
            "Use a single-config generator/build directory for generated per-source outputs.")
    endif()

    set(_gentest_has_module_sources FALSE)
    foreach(_gentest_module_name IN LISTS _gentest_module_names)
        if(NOT _gentest_module_name STREQUAL "__gentest_no_module__")
            set(_gentest_has_module_sources TRUE)
            break()
        endif()
    endforeach()

    set(_gentest_manifest_output "")
    set(_gentest_wrapper_cpp "")
    set(_gentest_wrapper_headers "")
    set(_gentest_extra_cpp "")
    set(_gentest_artifact_manifest "")
    set(_gentest_compile_context_ids "")
    set(_gentest_artifact_owner_sources "")
    set(_gentest_tu_manifest_enabled FALSE)
    if(_gentest_mode STREQUAL "manifest")
        _gentest_configure_manifest_mode(
            TARGET ${target}
            TARGET_ID ${_gentest_target_id}
            OUTPUT "${GENTEST_OUTPUT}"
            TUS ${_gentest_tus}
            OUT_OUTPUT _gentest_manifest_output
            OUT_OUTPUT_DIR _gentest_output_dir)
        message(WARNING
            "gentest_attach_codegen(${target}): OUTPUT selects legacy manifest/single-TU mode; "
            "omit OUTPUT or use OUTPUT_DIR for per-TU wrapper mode when supported. Manifest mode remains fallback-only.")
    elseif(_gentest_mode STREQUAL "module_registration")
        _gentest_prepare_module_registration_mode(
            TARGET ${target}
            TARGET_ID ${_gentest_target_id}
            OUTPUT_DIR "${GENTEST_OUTPUT_DIR}"
            TUS ${_gentest_tus}
            TU_SOURCE_ENTRIES ${_gentest_tu_source_entries}
            NEEDS_MODULE_SCAN ${_gentest_needs_module_scan}
            OUT_OUTPUT_DIR _gentest_output_dir
            OUT_REGISTRATION_CPP _gentest_wrapper_cpp
            OUT_WRAPPER_HEADERS _gentest_wrapper_headers
            OUT_MANIFEST _gentest_artifact_manifest
            OUT_COMPILE_CONTEXT_IDS _gentest_compile_context_ids)
    else()
        _gentest_prepare_tu_mode(
            TARGET ${target}
            TARGET_ID ${_gentest_target_id}
            OUTPUT_DIR "${GENTEST_OUTPUT_DIR}"
            NO_INCLUDE_SOURCES "${GENTEST_NO_INCLUDE_SOURCES}"
            TUS ${_gentest_tus}
            TU_SOURCE_ENTRIES ${_gentest_tu_source_entries}
            MODULE_NAMES ${_gentest_module_names}
            NEEDS_MODULE_SCAN ${_gentest_needs_module_scan}
            OUT_OUTPUT_DIR _gentest_output_dir
            OUT_WRAPPER_CPP _gentest_wrapper_cpp
            OUT_WRAPPER_HEADERS _gentest_wrapper_headers
            OUT_EXTRA_CPP _gentest_extra_cpp
            OUT_ARTIFACT_MANIFEST _gentest_artifact_manifest
            OUT_COMPILE_CONTEXT_IDS _gentest_compile_context_ids
            OUT_ARTIFACT_OWNER_SOURCES _gentest_artifact_owner_sources)
        if(NOT _gentest_has_module_sources)
            set(_gentest_tu_manifest_enabled TRUE)
        endif()
    endif()

    set(_gentest_module_wrapper_outputs "")
    if(_gentest_mode STREQUAL "tu")
        list(LENGTH _gentest_wrapper_cpp _gentest_wrapper_count)
        if(_gentest_wrapper_count GREATER 0)
            math(EXPR _gentest_last_wrapper "${_gentest_wrapper_count} - 1")
            foreach(_idx RANGE 0 ${_gentest_last_wrapper})
                list(GET _gentest_module_names ${_idx} _gentest_module_name)
                if(NOT _gentest_module_name STREQUAL "__gentest_no_module__")
                    list(GET _gentest_wrapper_cpp ${_idx} _gentest_module_wrapper_output)
                    list(APPEND _gentest_module_wrapper_outputs "${_gentest_module_wrapper_output}")
                endif()
            endforeach()
        endif()
    endif()

    set(_gentest_mock_registry "")
    set(_gentest_mock_impl "")
    set(_gentest_mock_registry_domain_outputs "")
    set(_gentest_mock_impl_domain_outputs "")
    if(NOT _gentest_mode STREQUAL "module_registration")
        set(_gentest_mock_registry "${_gentest_output_dir}/${_gentest_target_id}_mock_registry.hpp")
        # Generate inline mock implementations as a header; it will be included by
        # the generated wrapper translation units after including sources.
        set(_gentest_mock_impl "${_gentest_output_dir}/${_gentest_target_id}_mock_impl.hpp")
        _gentest_make_mock_domain_output_path("${_gentest_mock_registry}" 0 "header" _gentest_mock_registry_header_domain)
        _gentest_make_mock_domain_output_path("${_gentest_mock_impl}" 0 "header" _gentest_mock_impl_header_domain)
        list(APPEND _gentest_mock_registry_domain_outputs "${_gentest_mock_registry_header_domain}")
        list(APPEND _gentest_mock_impl_domain_outputs "${_gentest_mock_impl_header_domain}")

        set(_gentest_mock_seen_modules "")
        set(_gentest_mock_domain_idx 1)
        foreach(_gentest_module_name IN LISTS _gentest_module_names)
            if(_gentest_module_name STREQUAL "__gentest_no_module__")
                continue()
            endif()
            list(FIND _gentest_mock_seen_modules "${_gentest_module_name}" _gentest_mock_seen_idx)
            if(NOT _gentest_mock_seen_idx EQUAL -1)
                continue()
            endif()
            list(APPEND _gentest_mock_seen_modules "${_gentest_module_name}")

            _gentest_make_mock_domain_output_path("${_gentest_mock_registry}" ${_gentest_mock_domain_idx} "${_gentest_module_name}"
                _gentest_mock_registry_domain)
            _gentest_make_mock_domain_output_path("${_gentest_mock_impl}" ${_gentest_mock_domain_idx} "${_gentest_module_name}"
                _gentest_mock_impl_domain)
            list(APPEND _gentest_mock_registry_domain_outputs "${_gentest_mock_registry_domain}")
            list(APPEND _gentest_mock_impl_domain_outputs "${_gentest_mock_impl_domain}")

            string(MD5 _gentest_module_key "${_gentest_module_name}")
            set(_gentest_mock_domain_registry_${_gentest_module_key} "${_gentest_mock_registry_domain}")
            set(_gentest_mock_domain_impl_${_gentest_module_key} "${_gentest_mock_impl_domain}")
            math(EXPR _gentest_mock_domain_idx "${_gentest_mock_domain_idx} + 1")
        endforeach()
    endif()
    set(_gentest_depfile "${_gentest_output_dir}/${_gentest_target_id}.gentest.d")
    set(_gentest_mock_registration_manifest "")
    set(_gentest_mock_registration_manifest_depfile "")
    if(_gentest_mode STREQUAL "module_registration")
        set(_gentest_mock_registration_manifest "${_gentest_output_dir}/${_gentest_target_id}.mock_manifest.json")
        set(_gentest_mock_registration_manifest_depfile "${_gentest_output_dir}/${_gentest_target_id}.mock_manifest.d")
    endif()

    set(_gentest_codegen_scan_inputs "")
    list(LENGTH _gentest_tus _gentest_tu_count)
    math(EXPR _gentest_last_tu "${_gentest_tu_count} - 1")
    foreach(_gentest_idx RANGE 0 ${_gentest_last_tu})
        list(GET _gentest_tus ${_gentest_idx} _gentest_src_abs)
        list(GET _gentest_tu_source_entries ${_gentest_idx} _gentest_src_entry)
        list(GET _gentest_module_names ${_gentest_idx} _gentest_module_name)
        if(_gentest_mode STREQUAL "tu")
            list(GET _gentest_wrapper_cpp ${_gentest_idx} _gentest_wrap_cpp)
        endif()

        if(_gentest_module_name STREQUAL "__gentest_no_module__")
            if(_gentest_mode STREQUAL "tu")
                list(APPEND _gentest_codegen_scan_inputs ${_gentest_wrap_cpp})
            else()
                list(APPEND _gentest_codegen_scan_inputs ${_gentest_src_abs})
            endif()
        else()
            list(APPEND _gentest_codegen_scan_inputs ${_gentest_src_abs})
        endif()
    endforeach()

    set(_gentest_codegen_deps "")
    if(_gentest_codegen_target)
        list(APPEND _gentest_codegen_deps ${_gentest_codegen_target})
    endif()
    _gentest_collect_codegen_dep_targets_from_link_graph(
        ${target}
        _gentest_linked_codegen_dep_targets
        _gentest_linked_explicit_mock_targets)
    if(_gentest_linked_codegen_dep_targets)
        list(APPEND _gentest_codegen_deps ${_gentest_linked_codegen_dep_targets})
    endif()
    _gentest_collect_external_module_source_mappings(_gentest_external_module_source_mappings ${_gentest_linked_codegen_dep_targets})
    set_property(TARGET ${target} PROPERTY GENTEST_CODEGEN_EXTERNAL_MODULE_SOURCE_ARGS "")
    _gentest_append_external_module_source_args_property(${target} ${_gentest_external_module_source_mappings})
    if(EXISTS "${CMAKE_BINARY_DIR}/compile_commands.json")
        list(APPEND _gentest_codegen_deps "${CMAKE_BINARY_DIR}/compile_commands.json")
    endif()

    _gentest_make_codegen_command_launcher("${_gentest_codegen_executable}" _command_launcher)
    set(_gentest_codegen_tool_depends "")
    if(_gentest_codegen_target)
        list(APPEND _gentest_codegen_tool_depends "${_gentest_codegen_target}")
    elseif(NOT "${_gentest_codegen_executable}" MATCHES "\\$<")
        list(APPEND _gentest_codegen_tool_depends "${_gentest_codegen_executable}")
    endif()

    set(_command ${_command_launcher}
        --depfile ${_gentest_depfile}
        --compdb ${CMAKE_BINARY_DIR}
        --source-root ${CMAKE_SOURCE_DIR})
    if(_gentest_mock_registry AND _gentest_mock_impl)
        list(APPEND _command
            --mock-registry ${_gentest_mock_registry}
            --mock-impl ${_gentest_mock_impl})
    endif()

    if(_gentest_mode STREQUAL "manifest")
        list(APPEND _command --output ${_gentest_manifest_output})
        list(APPEND _command --entry ${GENTEST_ENTRY})
        if(GENTEST_NO_INCLUDE_SOURCES)
            list(APPEND _command --no-include-sources)
        endif()
    elseif(_gentest_mode STREQUAL "module_registration")
        list(APPEND _command --tu-out-dir ${_gentest_output_dir})
        list(APPEND _command --artifact-manifest ${_gentest_artifact_manifest})
        list(APPEND _command --mock-registration-manifest ${_gentest_mock_registration_manifest})
        foreach(_gentest_wrap_header IN LISTS _gentest_wrapper_headers)
            list(APPEND _command --tu-header-output ${_gentest_wrap_header})
        endforeach()
        foreach(_gentest_reg_cpp IN LISTS _gentest_wrapper_cpp)
            list(APPEND _command --module-registration-output ${_gentest_reg_cpp})
        endforeach()
        foreach(_gentest_context_id IN LISTS _gentest_compile_context_ids)
            list(APPEND _command --compile-context-id "${_gentest_context_id}")
        endforeach()
    else()
        list(APPEND _command --tu-out-dir ${_gentest_output_dir})
        if(_gentest_tu_manifest_enabled)
            list(APPEND _command --artifact-manifest ${_gentest_artifact_manifest})
        endif()
        foreach(_gentest_wrap_header IN LISTS _gentest_wrapper_headers)
            list(APPEND _command --tu-header-output ${_gentest_wrap_header})
        endforeach()
        foreach(_gentest_wrap_cpp IN LISTS _gentest_wrapper_cpp)
            list(APPEND _command --module-wrapper-output ${_gentest_wrap_cpp})
        endforeach()
        if(_gentest_tu_manifest_enabled)
            foreach(_gentest_owner_source IN LISTS _gentest_artifact_owner_sources)
                list(APPEND _command --artifact-owner-source "${_gentest_owner_source}")
            endforeach()
            foreach(_gentest_context_id IN LISTS _gentest_compile_context_ids)
                list(APPEND _command --compile-context-id "${_gentest_context_id}")
            endforeach()
        endif()
    endif()
    foreach(_gentest_mock_registry_domain_output IN LISTS _gentest_mock_registry_domain_outputs)
        list(APPEND _command --mock-domain-registry-output ${_gentest_mock_registry_domain_output})
    endforeach()
    foreach(_gentest_mock_impl_domain_output IN LISTS _gentest_mock_impl_domain_outputs)
        list(APPEND _command --mock-domain-impl-output ${_gentest_mock_impl_domain_output})
    endforeach()

    if(GENTEST_STRICT_FIXTURE)
        list(APPEND _command --strict-fixture)
    endif()
    if(GENTEST_QUIET_CLANG)
        list(APPEND _command --quiet-clang)
    endif()
    if(_gentest_attach_discovers_mocks)
        list(APPEND _command --discover-mocks)
    endif()
    _gentest_resolve_codegen_clang_scan_deps(_gentest_clang_scan_deps)
    _gentest_append_codegen_module_context_args(_command ${target} "${_gentest_clang_scan_deps}")

    if(_gentest_mode STREQUAL "tu")
        # Classic translation units are scanned via generated wrapper sources so
        # they inherit compdb-aligned flags. Named module units are scanned via
        # their original source path because their generated wrappers import the
        # module and therefore require built BMIs.
        list(APPEND _command ${_gentest_codegen_scan_inputs})
    else()
        list(APPEND _command ${_gentest_tus})
    endif()

    list(APPEND _command --)
    list(APPEND _command ${_gentest_source_inspection_clang_args})
    list(APPEND _command ${_gentest_source_inspection_system_include_args})

    cmake_policy(PUSH)
    if(POLICY CMP0171)
        cmake_policy(SET CMP0171 NEW)
    endif()

    if(_gentest_mock_registration_manifest)
        set(_gentest_mock_inspect_command ${_command_launcher}
            inspect-mocks
            --mock-manifest-output ${_gentest_mock_registration_manifest}
            --depfile ${_gentest_mock_registration_manifest_depfile}
            --compdb ${CMAKE_BINARY_DIR}
            --source-root ${CMAKE_SOURCE_DIR})
        if(GENTEST_QUIET_CLANG)
            list(APPEND _gentest_mock_inspect_command --quiet-clang)
        endif()
        _gentest_append_codegen_module_context_args(_gentest_mock_inspect_command ${target} "${_gentest_clang_scan_deps}")
        list(APPEND _gentest_mock_inspect_command ${_gentest_tus})
        list(APPEND _gentest_mock_inspect_command --)
        list(APPEND _gentest_mock_inspect_command ${_gentest_source_inspection_clang_args})
        list(APPEND _gentest_mock_inspect_command ${_gentest_source_inspection_system_include_args})

        set(_gentest_mock_inspect_command_args
            OUTPUT ${_gentest_mock_registration_manifest}
            COMMAND ${_gentest_mock_inspect_command}
            COMMAND_EXPAND_LISTS
            DEPENDS
                ${_gentest_codegen_deps}
                ${_gentest_codegen_tool_depends}
                ${_gentest_tus}
                ${GENTEST_DEPENDS}
                "$<$<BOOL:$<TARGET_PROPERTY:${target},GENTEST_CODEGEN_EXTRA_DEPENDS>>:$<TARGET_PROPERTY:${target},GENTEST_CODEGEN_EXTRA_DEPENDS>>"
            COMMENT "Inspecting gentest mocks for target ${target}"
            VERBATIM)
        if(CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
            list(APPEND _gentest_mock_inspect_command_args DEPFILE ${_gentest_mock_registration_manifest_depfile})
        endif()
        if(POLICY CMP0171)
            list(APPEND _gentest_mock_inspect_command_args CODEGEN)
        endif()
        add_custom_command(${_gentest_mock_inspect_command_args})
        unset(_gentest_mock_inspect_command_args)
    endif()

    if(_gentest_mode STREQUAL "manifest")
        set(_gentest_codegen_outputs
            ${_gentest_manifest_output}
            ${_gentest_mock_registry}
            ${_gentest_mock_impl}
            ${_gentest_mock_registry_domain_outputs}
            ${_gentest_mock_impl_domain_outputs})
    elseif(_gentest_mode STREQUAL "module_registration")
        set(_gentest_codegen_outputs
            ${_gentest_wrapper_headers}
            ${_gentest_wrapper_cpp}
            ${_gentest_artifact_manifest})
    else()
        set(_gentest_codegen_outputs
            ${_gentest_wrapper_headers}
            ${_gentest_module_wrapper_outputs}
            ${_gentest_mock_registry}
            ${_gentest_mock_impl}
            ${_gentest_mock_registry_domain_outputs}
            ${_gentest_mock_impl_domain_outputs})
        if(_gentest_tu_manifest_enabled)
            list(APPEND _gentest_codegen_outputs ${_gentest_artifact_manifest})
        endif()
    endif()
    set(_gentest_all_codegen_outputs ${_gentest_codegen_outputs})
    if(_gentest_mock_registration_manifest)
        list(APPEND _gentest_all_codegen_outputs ${_gentest_mock_registration_manifest})
    endif()
    set(_gentest_codegen_target_depends ${_gentest_all_codegen_outputs})
    set_property(TARGET ${target} PROPERTY GENTEST_CODEGEN_OUTPUTS "${_gentest_all_codegen_outputs}")
    set_property(TARGET ${target} PROPERTY GENTEST_CODEGEN_EXTRA_DEPENDS "")
    set(_gentest_custom_command_args
        OUTPUT ${_gentest_codegen_outputs}
        COMMAND ${_command}
        COMMAND_EXPAND_LISTS
        DEPENDS
            ${_gentest_codegen_deps}
            ${_gentest_codegen_tool_depends}
            ${_gentest_tus}
            ${_gentest_mock_registration_manifest}
            ${GENTEST_DEPENDS}
            "$<$<BOOL:$<TARGET_PROPERTY:${target},GENTEST_CODEGEN_EXTRA_DEPENDS>>:$<TARGET_PROPERTY:${target},GENTEST_CODEGEN_EXTRA_DEPENDS>>"
        COMMENT "Running gentest_codegen for target ${target}"
        VERBATIM)
    if(CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
        list(APPEND _gentest_custom_command_args DEPFILE ${_gentest_depfile})
    endif()
    if(POLICY CMP0171)
        list(APPEND _gentest_custom_command_args CODEGEN)
    endif()
    add_custom_command(${_gentest_custom_command_args})
    unset(_gentest_custom_command_args)

    if(_gentest_mode STREQUAL "module_registration")
        set(_gentest_manifest_validation_stamp "${_gentest_output_dir}/${_gentest_target_id}.artifact_manifest.validated")
        set(_gentest_manifest_source_kinds "")
        foreach(_gentest_unused IN LISTS _gentest_tus)
            list(APPEND _gentest_manifest_source_kinds "module-primary-interface")
        endforeach()
        _gentest_make_artifact_manifest_validation_args(
            MANIFEST "${_gentest_artifact_manifest}"
            STAMP "${_gentest_manifest_validation_stamp}"
            INCLUDE_DIR "${_gentest_output_dir}"
            DEPFILE "${_gentest_depfile}"
            TARGET_ATTACHMENT "private-generated-source"
            ARTIFACT_ROLE "registration"
            COMPILE_AS "cxx-module-implementation"
            REQUIRES_MODULE_SCAN "ON"
            COMPDB "${CMAKE_BINARY_DIR}"
            SOURCES ${_gentest_tus}
            SOURCE_KINDS ${_gentest_manifest_source_kinds}
            REGISTRATION_OUTPUTS ${_gentest_wrapper_cpp}
            HEADERS ${_gentest_wrapper_headers}
            COMPILE_CONTEXT_IDS ${_gentest_compile_context_ids}
            SOURCE_REGISTRATION_OUTPUTS ${_gentest_wrapper_cpp}
            SCAN_CONTEXT_ARGS ${_gentest_manifest_validation_scan_context_args}
            OUT_ARGS _gentest_manifest_validation_args)
        _gentest_add_artifact_manifest_validation_command(
            STAMP "${_gentest_manifest_validation_stamp}"
            COMMAND_LAUNCHER ${_command_launcher}
            VALIDATION_ARGS ${_gentest_manifest_validation_args}
            DEPENDS
                ${_gentest_codegen_outputs}
                ${_gentest_codegen_tool_depends}
            COMMENT "Validating gentest_codegen artifact manifest for target ${target}")
        list(APPEND _gentest_codegen_target_depends "${_gentest_manifest_validation_stamp}")
    elseif(_gentest_tu_manifest_enabled)
        set(_gentest_manifest_validation_stamp "${_gentest_output_dir}/${_gentest_target_id}.artifact_manifest.validated")
        set(_gentest_manifest_source_kinds "")
        foreach(_gentest_unused IN LISTS _gentest_codegen_scan_inputs)
            list(APPEND _gentest_manifest_source_kinds "textual-wrapper")
        endforeach()
        _gentest_make_artifact_manifest_validation_args(
            MANIFEST "${_gentest_artifact_manifest}"
            STAMP "${_gentest_manifest_validation_stamp}"
            INCLUDE_DIR "${_gentest_output_dir}"
            DEPFILE "${_gentest_depfile}"
            TARGET_ATTACHMENT "replace-owner-source"
            ARTIFACT_ROLE "registration"
            COMPILE_AS "cxx-textual-wrapper"
            REQUIRES_MODULE_SCAN "OFF"
            INCLUDES_OWNER_SOURCE "ON"
            REPLACES_OWNER_SOURCE "ON"
            SOURCES ${_gentest_codegen_scan_inputs}
            SOURCE_KINDS ${_gentest_manifest_source_kinds}
            REGISTRATION_OUTPUTS ${_gentest_codegen_scan_inputs}
            HEADERS ${_gentest_wrapper_headers}
            COMPILE_CONTEXT_IDS ${_gentest_compile_context_ids}
            OWNER_SOURCES ${_gentest_artifact_owner_sources}
            OUT_ARGS _gentest_manifest_validation_args)
        _gentest_add_artifact_manifest_validation_command(
            STAMP "${_gentest_manifest_validation_stamp}"
            COMMAND_LAUNCHER ${_command_launcher}
            VALIDATION_ARGS ${_gentest_manifest_validation_args}
            DEPENDS
                ${_gentest_codegen_outputs}
                ${_gentest_codegen_tool_depends}
            COMMENT "Validating gentest_codegen textual artifact manifest for target ${target}")
        list(APPEND _gentest_codegen_target_depends "${_gentest_manifest_validation_stamp}")
    endif()

    cmake_policy(POP)

    if(_gentest_mode STREQUAL "manifest")
        _gentest_attach_manifest_codegen(TARGET ${target} OUTPUT ${_gentest_manifest_output})
    elseif(_gentest_mode STREQUAL "module_registration")
        _gentest_attach_module_registration_sources(
            TARGET ${target}
            TARGET_ID ${_gentest_target_id}
            REGISTRATION_CPP ${_gentest_wrapper_cpp}
            CODEGEN_OUTPUTS ${_gentest_codegen_target_depends})
    else()
        _gentest_attach_tu_wrapper_sources(
            TARGET ${target}
            TARGET_ID ${_gentest_target_id}
            REPLACED_TUS ${_gentest_tus}
            REPLACED_SOURCE_ENTRIES ${_gentest_tu_source_entries}
            WRAPPER_CPP ${_gentest_wrapper_cpp}
            MODULE_NAMES ${_gentest_module_names}
            EXTRA_CPP ${_gentest_extra_cpp}
            CODEGEN_OUTPUTS ${_gentest_codegen_target_depends})
    endif()

    if(_gentest_attach_discovers_mocks AND _gentest_module_wrapper_outputs)
        set(_gentest_explicit_mock_module_build_sources "")
        set(_gentest_explicit_mock_module_rel_sources "")
        list(LENGTH _gentest_wrapper_cpp _gentest_wrapper_count)
        math(EXPR _gentest_last_wrapper_idx "${_gentest_wrapper_count} - 1")
        foreach(_gentest_idx RANGE 0 ${_gentest_last_wrapper_idx})
            list(GET _gentest_module_names ${_gentest_idx} _gentest_module_name)
            if(_gentest_module_name STREQUAL "__gentest_no_module__")
                continue()
            endif()
            list(GET _gentest_wrapper_cpp ${_gentest_idx} _gentest_module_wrapper)
            list(APPEND _gentest_explicit_mock_module_build_sources "${_gentest_module_name}=${_gentest_module_wrapper}")
            file(RELATIVE_PATH _gentest_module_wrapper_rel "${_gentest_output_dir}" "${_gentest_module_wrapper}")
            list(APPEND _gentest_explicit_mock_module_rel_sources "${_gentest_module_name}=${_gentest_module_wrapper_rel}")
        endforeach()
        if(_gentest_explicit_mock_module_build_sources)
            _gentest_append_target_list_property(
                ${target}
                GENTEST_EXPLICIT_MOCK_MODULE_BUILD_SOURCES
                ${_gentest_explicit_mock_module_build_sources})
            _gentest_append_target_list_property(
                ${target}
                GENTEST_EXPLICIT_MOCK_MODULE_REL_SOURCES
                ${_gentest_explicit_mock_module_rel_sources})
            _gentest_append_target_export_property(${target} GENTEST_EXPLICIT_MOCK_MODULE_REL_SOURCES)
        endif()
    endif()

    if(_gentest_mock_registry AND _gentest_mock_impl)
        get_filename_component(_gentest_mock_dir "${_gentest_mock_registry}" DIRECTORY)
        include(GNUInstallDirs)
        target_include_directories(${target}
            PRIVATE
                "$<BUILD_INTERFACE:${_gentest_mock_dir}>"
                "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

        get_filename_component(_gentest_mock_header_name "${_gentest_mock_registry}" NAME)
        get_filename_component(_gentest_mock_impl_name "${_gentest_mock_impl}" NAME)
        target_compile_definitions(${target} PRIVATE
            GENTEST_MOCK_REGISTRY_PATH=${_gentest_mock_header_name}
            GENTEST_MOCK_IMPL_PATH=${_gentest_mock_impl_name}
        )
    endif()
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

function(gentest_add_mocks target)
    set(options)
    set(one_value_args OUTPUT_DIR MODULE_NAME HEADER_NAME)
    set(multi_value_args DEFS CLANG_ARGS DEPENDS LINK_LIBRARIES)
    cmake_parse_arguments(GENTEST "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(TARGET ${target})
        message(FATAL_ERROR "gentest_add_mocks(${target}): target already exists")
    endif()
    if(NOT GENTEST_DEFS)
        message(FATAL_ERROR "gentest_add_mocks(${target}): DEFS is required")
    endif()
    if(CMAKE_CONFIGURATION_TYPES)
        message(FATAL_ERROR
            "gentest_add_mocks(${target}): explicit mock targets currently require a single-config generator. "
            "Multi-config generators are not supported because gentest_attach_codegen() runs in TU-wrapper mode here.")
    endif()

    string(MAKE_C_IDENTIFIER "${target}" _gentest_target_id)

    if(GENTEST_OUTPUT_DIR)
        set(_gentest_output_dir "${GENTEST_OUTPUT_DIR}")
    else()
        set(_gentest_output_dir "${CMAKE_CURRENT_BINARY_DIR}/gentest_mocks/${_gentest_target_id}")
    endif()
    if("${_gentest_output_dir}" MATCHES "\\$<")
        message(FATAL_ERROR
            "gentest_add_mocks(${target}): OUTPUT_DIR contains generator expressions, which is not supported. "
            "Pass a concrete path instead.")
    endif()
    _gentest_normalize_path_and_key("${_gentest_output_dir}" "${CMAKE_CURRENT_BINARY_DIR}" _gentest_output_dir_abs _gentest_output_dir_key)
    _gentest_reserve_unique_owner("GENTEST_EXPLICIT_MOCK_OUTDIR_OWNER" "${_gentest_output_dir_key}" "${target}" _gentest_prev_owner)
    if(_gentest_prev_owner AND NOT _gentest_prev_owner STREQUAL "${target}")
        message(FATAL_ERROR
            "gentest_add_mocks(${target}): OUTPUT_DIR '${_gentest_output_dir_abs}' is already used by '${_gentest_prev_owner}'. "
            "Each explicit mock target must have a unique OUTPUT_DIR.")
    endif()
    set(_gentest_output_dir "${_gentest_output_dir_abs}")
    file(MAKE_DIRECTORY "${_gentest_output_dir}")

    set(_gentest_textual_defs "")
    set(_gentest_module_defs "")
    set(_gentest_module_def_names "")
    set(_gentest_mock_surface_module_defs "")
    set(_gentest_mock_surface_module_names "")
    _gentest_collect_common_codegen_clang_args(_gentest_mock_inspection_clang_args)
    _gentest_collect_common_codegen_system_include_args(_gentest_mock_inspection_system_include_args)
    _gentest_collect_scan_macro_args_from_sequence(_gentest_mock_scan_macro_args _gentest_mock_scan_macro_has_genex
        ${_gentest_mock_inspection_clang_args})
    if(_gentest_mock_scan_macro_has_genex)
        message(FATAL_ERROR
            "gentest_add_mocks(${target}): source inspection for module DEFS does not support generator-expression macro state in "
            "CLANG_ARGS. Use concrete macro arguments for module DEFS that conditionally expose gentest.mock imports.")
    endif()
    _gentest_collect_scan_include_dirs_from_sequence("${CMAKE_CURRENT_SOURCE_DIR}" _gentest_mock_scan_include_dirs
        ${_gentest_mock_inspection_clang_args}
        ${_gentest_mock_inspection_system_include_args})
    foreach(_gentest_def IN LISTS GENTEST_DEFS)
        if("${_gentest_def}" MATCHES "\\$<")
            message(FATAL_ERROR
                "gentest_add_mocks(${target}): generator-expression DEFS entries are not supported. "
                "Pass concrete files instead: '${_gentest_def}'")
        endif()
        _gentest_normalize_path_and_key("${_gentest_def}" "${CMAKE_CURRENT_SOURCE_DIR}" _gentest_def_abs _gentest_def_key)
        get_filename_component(_gentest_def_ext "${_gentest_def_abs}" EXT)
        if(_gentest_def_ext MATCHES "^\\.(cppm|ccm|cxxm|ixx|mxx)$")
            list(APPEND _gentest_module_defs "${_gentest_def_abs}")
            _gentest_try_extract_module_name("${_gentest_def_abs}" _gentest_module_name
                ${_gentest_mock_scan_include_dirs}
                __GENTEST_SCAN_COMMAND_LINE__
                ${_gentest_mock_scan_macro_args})
            if(_gentest_module_name STREQUAL "")
                message(FATAL_ERROR
                    "gentest_add_mocks(${target}): failed to extract a named module declaration from '${_gentest_def_abs}'. "
                    "Explicit mock module defs must declare `export module ...;`.")
            endif()
            list(APPEND _gentest_module_def_names "${_gentest_module_name}")
            _gentest_file_imports_gentest_mock("${_gentest_def_abs}" _gentest_imports_gentest_mock
                ${_gentest_mock_scan_include_dirs}
                __GENTEST_SCAN_COMMAND_LINE__
                ${_gentest_mock_scan_macro_args})
            if(_gentest_imports_gentest_mock)
                list(APPEND _gentest_mock_surface_module_defs "${_gentest_def_abs}")
                list(APPEND _gentest_mock_surface_module_names "${_gentest_module_name}")
            endif()
        else()
            list(APPEND _gentest_textual_defs "${_gentest_def_abs}")
        endif()
    endforeach()

    if(_gentest_textual_defs AND _gentest_module_defs)
        message(FATAL_ERROR
            "gentest_add_mocks(${target}): mixed textual and module DEFS are not supported. "
            "Split them into separate explicit mock targets.")
    endif()
    if(_gentest_module_defs)
        if(GENTEST_HEADER_NAME)
            message(FATAL_ERROR
                "gentest_add_mocks(${target}): HEADER_NAME is only supported for textual DEFS files. "
                "Module DEFS publish a named-module surface instead.")
        endif()
        if(NOT GENTEST_MODULE_NAME)
            message(FATAL_ERROR
                "gentest_add_mocks(${target}): MODULE_NAME is required when DEFS contain named modules.")
        endif()
        if(NOT _gentest_mock_surface_module_defs)
            message(FATAL_ERROR
                "gentest_add_mocks(${target}): module DEFS must include at least one mock-surface module that "
                "`import`s or `export import`s `gentest.mock`. Keep provider/support modules in DEFS if needed, "
                "but include one module that forms the public mock surface too.")
        endif()
    elseif(GENTEST_MODULE_NAME)
        message(FATAL_ERROR
            "gentest_add_mocks(${target}): MODULE_NAME is not yet supported for textual DEFS files. "
            "Use module DEFS when you need a public named-module surface, or omit MODULE_NAME.")
    endif()

    set(_gentest_public_header "")
    set(_gentest_public_header_dir "")
    if(_gentest_textual_defs)
        if(GENTEST_HEADER_NAME)
            if(IS_ABSOLUTE "${GENTEST_HEADER_NAME}")
                message(FATAL_ERROR
                    "gentest_add_mocks(${target}): HEADER_NAME must be a filename or relative path within OUTPUT_DIR. "
                    "Absolute HEADER_NAME values are not supported: '${GENTEST_HEADER_NAME}'")
            endif()
            set(_gentest_public_header_name "${GENTEST_HEADER_NAME}")
        else()
            set(_gentest_public_header_name "${_gentest_target_id}.hpp")
        endif()
        _gentest_normalize_path_and_key("${_gentest_public_header_name}" "${_gentest_output_dir}" _gentest_public_header _gentest_public_header_key)
        file(RELATIVE_PATH _gentest_public_header_rel_from_output "${_gentest_output_dir}" "${_gentest_public_header}")
        if(_gentest_public_header_rel_from_output MATCHES "^\\.\\.")
            message(FATAL_ERROR
                "gentest_add_mocks(${target}): HEADER_NAME must stay within OUTPUT_DIR '${_gentest_output_dir}'. "
                "Got '${_gentest_public_header}'.")
        endif()
        get_filename_component(_gentest_public_header_dir "${_gentest_public_header}" DIRECTORY)
        set(_gentest_reserved_output_paths
            "${_gentest_output_dir}/${_gentest_target_id}_anchor.cpp"
            "${_gentest_output_dir}/${_gentest_target_id}_defs.cpp"
            "${_gentest_output_dir}/${_gentest_target_id}_mock_registry.hpp"
            "${_gentest_output_dir}/${_gentest_target_id}_mock_impl.hpp"
            "${_gentest_output_dir}/${_gentest_target_id}.cppm")
        foreach(_gentest_reserved_output IN LISTS _gentest_reserved_output_paths)
            _gentest_normalize_path_and_key("${_gentest_reserved_output}" "${_gentest_output_dir}" _gentest_reserved_output_abs _gentest_reserved_output_key)
            if(_gentest_public_header_key STREQUAL _gentest_reserved_output_key)
                message(FATAL_ERROR
                    "gentest_add_mocks(${target}): HEADER_NAME '${_gentest_public_header_name}' collides with a reserved generated output "
                    "'${_gentest_reserved_output_abs}'. Choose a different HEADER_NAME.")
            endif()
        endforeach()
    endif()

    set(_gentest_explicit_mock_search_roots
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}")
    foreach(_gentest_def_path IN LISTS _gentest_textual_defs _gentest_module_defs)
        get_filename_component(_gentest_def_dir "${_gentest_def_path}" DIRECTORY)
        if(NOT _gentest_def_dir STREQUAL "")
            list(APPEND _gentest_explicit_mock_search_roots "${_gentest_def_dir}")
        endif()
    endforeach()
    foreach(_gentest_link_library IN LISTS GENTEST_LINK_LIBRARIES)
        if(NOT TARGET "${_gentest_link_library}")
            continue()
        endif()
        foreach(_gentest_include_property IN ITEMS INTERFACE_INCLUDE_DIRECTORIES INCLUDE_DIRECTORIES)
            get_target_property(_gentest_link_include_dirs "${_gentest_link_library}" "${_gentest_include_property}")
            if(NOT _gentest_link_include_dirs OR _gentest_link_include_dirs MATCHES "-NOTFOUND$")
                continue()
            endif()
            foreach(_gentest_link_include_dir IN LISTS _gentest_link_include_dirs)
                _gentest_expand_explicit_mock_search_root("${_gentest_link_include_dir}" _gentest_expanded_include_roots)
                list(APPEND _gentest_explicit_mock_search_roots ${_gentest_expanded_include_roots})
            endforeach()
        endforeach()
    endforeach()
    list(REMOVE_DUPLICATES _gentest_explicit_mock_search_roots)
    string(MD5 _gentest_search_roots_key "${_gentest_output_dir}/defs")
    set_property(GLOBAL PROPERTY "GENTEST_EXPLICIT_MOCK_SEARCH_ROOTS_${_gentest_search_roots_key}" "${_gentest_explicit_mock_search_roots}")

    _gentest_materialize_explicit_mock_defs("${_gentest_output_dir}" _gentest_materialized_textual_defs _gentest_textual_public_files ${_gentest_textual_defs})
    _gentest_materialize_explicit_module_mock_defs(
        "${_gentest_output_dir}"
        _gentest_materialized_module_defs
        _gentest_module_public_files
        ${_gentest_module_defs})
    if(_gentest_materialized_module_defs)
        list(LENGTH _gentest_materialized_module_defs _gentest_materialized_module_count)
        math(EXPR _gentest_last_materialized_module "${_gentest_materialized_module_count} - 1")
        foreach(_gentest_module_idx RANGE 0 ${_gentest_last_materialized_module})
            list(GET _gentest_materialized_module_defs ${_gentest_module_idx} _gentest_materialized_module_def)
            list(GET _gentest_module_def_names ${_gentest_module_idx} _gentest_materialized_module_name)
            set_source_files_properties("${_gentest_materialized_module_def}" PROPERTIES
                GENTEST_MODULE_NAME "${_gentest_materialized_module_name}")
        endforeach()
    endif()

    set(_gentest_codegen_sources "${_gentest_materialized_module_defs}")

    if(_gentest_textual_defs)
        set(_gentest_public_header_content
"// This file is auto-generated by gentest (explicit mocks surface).\n\
// Do not edit manually.\n\
\n\
#pragma once\n\
\n")
        string(APPEND _gentest_public_header_content "#define GENTEST_NO_AUTO_MOCK_INCLUDE 1\n")
        string(APPEND _gentest_public_header_content "#include \"gentest/mock.h\"\n")
        foreach(_gentest_def IN LISTS _gentest_materialized_textual_defs)
            file(RELATIVE_PATH _gentest_public_def_include "${_gentest_public_header_dir}" "${_gentest_def}")
            string(APPEND _gentest_public_header_content "#include \"${_gentest_public_def_include}\"\n")
        endforeach()
        string(APPEND _gentest_public_header_content "#undef GENTEST_NO_AUTO_MOCK_INCLUDE\n\n")
        file(RELATIVE_PATH _gentest_public_registry_include "${_gentest_public_header_dir}" "${_gentest_output_dir}/${_gentest_target_id}_mock_registry.hpp")
        file(RELATIVE_PATH _gentest_public_impl_include "${_gentest_public_header_dir}" "${_gentest_output_dir}/${_gentest_target_id}_mock_impl.hpp")
        string(APPEND _gentest_public_header_content "#include \"${_gentest_public_registry_include}\"\n")
        string(APPEND _gentest_public_header_content "#include \"${_gentest_public_impl_include}\"\n")
        file(WRITE "${_gentest_public_header}" "${_gentest_public_header_content}")
    endif()

    set(_gentest_textual_wrapper "${_gentest_output_dir}/${_gentest_target_id}_defs.cpp")
    if(_gentest_textual_defs)
        set(_gentest_textual_wrapper_content
"// This file is auto-generated by gentest (explicit mocks surface).\n\
// Do not edit manually.\n\
\n\
#include \"gentest/mock.h\"\n")
        foreach(_gentest_def IN LISTS _gentest_materialized_textual_defs)
            file(RELATIVE_PATH _gentest_textual_def_include "${_gentest_output_dir}" "${_gentest_def}")
            string(APPEND _gentest_textual_wrapper_content "#include \"${_gentest_textual_def_include}\"\n")
        endforeach()
        file(WRITE "${_gentest_textual_wrapper}" "${_gentest_textual_wrapper_content}")
        list(APPEND _gentest_codegen_sources "${_gentest_textual_wrapper}")
    endif()

    set(_gentest_anchor_cpp "${_gentest_output_dir}/${_gentest_target_id}_anchor.cpp")
    set(_gentest_anchor_content
"// This file is auto-generated by gentest (explicit mocks anchor).\n\
// Do not edit manually.\n\
\n\
namespace gentest::detail {\n\
int ${_gentest_target_id}_explicit_mock_anchor = 0;\n\
} // namespace gentest::detail\n")
    file(WRITE "${_gentest_anchor_cpp}" "${_gentest_anchor_content}")

    add_library(${target} STATIC)
    set_target_properties(${target} PROPERTIES GENTEST_EXPLICIT_MOCK_TARGET TRUE)
    _gentest_append_target_export_property(${target} GENTEST_EXPLICIT_MOCK_TARGET)
    target_compile_features(${target} PUBLIC cxx_std_20)
    target_link_libraries(${target} PUBLIC gentest::gentest)
    if(GENTEST_LINK_LIBRARIES)
        target_link_libraries(${target} PUBLIC ${GENTEST_LINK_LIBRARIES})
    endif()
    target_sources(${target} PRIVATE "${_gentest_anchor_cpp}")
    if(_gentest_textual_defs)
        target_sources(${target} PRIVATE "${_gentest_textual_wrapper}")
    endif()
    if(_gentest_materialized_module_defs)
        target_sources(${target}
            PUBLIC
                FILE_SET gentest_explicit_mock_modules_private
                    TYPE CXX_MODULES
                    BASE_DIRS "${_gentest_output_dir}"
                    FILES ${_gentest_materialized_module_defs})
    endif()

    gentest_attach_codegen(${target}
        OUTPUT_DIR "${_gentest_output_dir}"
        SOURCES ${_gentest_codegen_sources}
        CLANG_ARGS ${GENTEST_CLANG_ARGS}
        DEPENDS ${GENTEST_DEPENDS})

    set(_gentest_mock_registry "${_gentest_output_dir}/${_gentest_target_id}_mock_registry.hpp")
    set(_gentest_mock_impl "${_gentest_output_dir}/${_gentest_target_id}_mock_impl.hpp")
    get_filename_component(_gentest_mock_registry_name "${_gentest_mock_registry}" NAME)
    get_filename_component(_gentest_mock_impl_name "${_gentest_mock_impl}" NAME)

    if(_gentest_textual_defs)
        set_source_files_properties("${_gentest_public_header}" PROPERTIES GENERATED TRUE)
        _gentest_make_mock_domain_output_path("${_gentest_mock_registry}" 0 "header" _gentest_mock_registry_header_domain)
        _gentest_make_mock_domain_output_path("${_gentest_mock_impl}" 0 "header" _gentest_mock_impl_header_domain)
        set_source_files_properties(
            "${_gentest_mock_registry}"
            "${_gentest_mock_impl}"
            "${_gentest_mock_registry_header_domain}"
            "${_gentest_mock_impl_header_domain}"
            PROPERTIES GENERATED TRUE)
        target_sources(${target}
            PUBLIC
                FILE_SET gentest_explicit_mock_headers
                    TYPE HEADERS
                    BASE_DIRS "${_gentest_output_dir}"
                    FILES
                        "${_gentest_public_header}"
                        ${_gentest_textual_public_files}
                        "${_gentest_mock_registry}"
                        "${_gentest_mock_impl}"
                        "${_gentest_mock_registry_header_domain}"
                        "${_gentest_mock_impl_header_domain}")
    endif()

    set(_gentest_module_support_headers "")
    foreach(_gentest_module_public_file IN LISTS _gentest_module_public_files)
        list(FIND _gentest_materialized_module_defs "${_gentest_module_public_file}" _gentest_module_public_file_index)
        if(_gentest_module_public_file_index EQUAL -1)
            string(FIND "${_gentest_module_public_file}" "${_gentest_output_dir}/defs/deps/" _gentest_defs_deps_pos)
            if(_gentest_defs_deps_pos EQUAL 0)
                continue()
            endif()
            list(APPEND _gentest_module_support_headers "${_gentest_module_public_file}")
        endif()
    endforeach()
    if(_gentest_materialized_module_defs)
        _gentest_make_mock_domain_output_path("${_gentest_mock_registry}" 0 "header" _gentest_mock_registry_header_domain)
        _gentest_make_mock_domain_output_path("${_gentest_mock_impl}" 0 "header" _gentest_mock_impl_header_domain)
        list(APPEND _gentest_module_support_headers
            "${_gentest_mock_registry}"
            "${_gentest_mock_impl}"
            "${_gentest_mock_registry_header_domain}"
            "${_gentest_mock_impl_header_domain}")
    endif()
    if(_gentest_module_support_headers)
        set_source_files_properties(${_gentest_module_support_headers} PROPERTIES GENERATED TRUE)
        target_sources(${target}
            PUBLIC
                FILE_SET gentest_explicit_mock_module_headers
                    TYPE HEADERS
                    BASE_DIRS "${_gentest_output_dir}"
                    FILES ${_gentest_module_support_headers})
    endif()

    if(GENTEST_MODULE_NAME)
        set(_gentest_aggregate_module_rel "${GENTEST_MODULE_NAME}")
        string(REPLACE "." "/" _gentest_aggregate_module_rel "${_gentest_aggregate_module_rel}")
        string(REPLACE ":" "/" _gentest_aggregate_module_rel "${_gentest_aggregate_module_rel}")
        set(_gentest_aggregate_module "${_gentest_output_dir}/${_gentest_aggregate_module_rel}.cppm")
        get_filename_component(_gentest_aggregate_module_dir "${_gentest_aggregate_module}" DIRECTORY)
        file(MAKE_DIRECTORY "${_gentest_aggregate_module_dir}")
        set(_gentest_aggregate_module_content
"// This file is auto-generated by gentest (explicit mocks aggregate module).\n\
// Do not edit manually.\n\
\n\
module;\n\
\n\
export module ${GENTEST_MODULE_NAME};\n\
\n")
        string(APPEND _gentest_aggregate_module_content "export import gentest;\n")
        string(APPEND _gentest_aggregate_module_content "export import gentest.mock;\n")
        foreach(_gentest_module_name IN LISTS _gentest_module_def_names)
            string(APPEND _gentest_aggregate_module_content "export import ${_gentest_module_name};\n")
        endforeach()
        file(WRITE "${_gentest_aggregate_module}" "${_gentest_aggregate_module_content}")
        set_source_files_properties("${_gentest_aggregate_module}" PROPERTIES GENERATED TRUE)
        target_sources(${target}
            PUBLIC
                FILE_SET gentest_explicit_mock_aggregate_module
                    TYPE CXX_MODULES
                    BASE_DIRS "${_gentest_output_dir}"
                    FILES "${_gentest_aggregate_module}")
        file(RELATIVE_PATH _gentest_aggregate_module_rel_from_output "${_gentest_output_dir}" "${_gentest_aggregate_module}")
        _gentest_append_target_list_property(
            ${target}
            GENTEST_EXPLICIT_MOCK_MODULE_BUILD_SOURCES
            "${GENTEST_MODULE_NAME}=${_gentest_aggregate_module}")
        _gentest_append_target_list_property(
            ${target}
            GENTEST_EXPLICIT_MOCK_MODULE_REL_SOURCES
            "${GENTEST_MODULE_NAME}=${_gentest_aggregate_module_rel_from_output}")
        _gentest_append_target_export_property(${target} GENTEST_EXPLICIT_MOCK_MODULE_REL_SOURCES)
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
#  -DDEATH_EXPECT_SUBSTRING=<substring expected in combined output>

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

if(DEFINED DEATH_EXPECT_SUBSTRING)
  string(FIND "${_all}" "${DEATH_EXPECT_SUBSTRING}" _pos)
  if(_pos EQUAL -1)
    message(FATAL_ERROR "Expected substring not found in output: '${DEATH_EXPECT_SUBSTRING}'. Output:\n${_all}")
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
    DEATH_EXPECT_SUBSTRING
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
      COMMAND ${launcher_args} [==[${arg_TEST_EXECUTABLE}]==] --list ${discovery_extra_args}
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
      "  Command: --list\n"
      "  Stdout:\n"
      "    ${output}\n"
      "  Stderr:\n"
      "    ${error_output}\n"
    )
  endif()

  set(filter_regex "")
  if(arg_TEST_FILTER)
    _gentest_wildcard_to_regex(filter_regex "${arg_TEST_FILTER}")
  endif()

  set(_combined_output "${output}")
  _gentest_generate_testname_guards("${_combined_output}" open_guard close_guard)

  function(_gentest_parse_meta_list raw_output out_var out_death_var)
    set(_meta_prefix " [gentest:")
    string(LENGTH "${_meta_prefix}" _meta_prefix_len)
    set(_out "${raw_output}")
    _gentest_escape_square_brackets("${_out}" "[" "__osb" open_sb _out)
    _gentest_escape_square_brackets("${_out}" "]" "__csb" close_sb _out)
    string(REPLACE [[;]] [[\;]] _out "${_out}")
    string(REPLACE "\r\n" "\n" _out "${_out}")
    string(REPLACE "\n" ";" _out "${_out}")

    set(_cases "")
    set(_death_cases "")
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

      set(case_body "${case_line}")
      if(case_body MATCHES "^(.*) \\(.+:[0-9]+\\)$")
        set(case_body "${CMAKE_MATCH_1}")
      endif()

      set(case_name "${case_body}")
      set(case_meta "")
      string(FIND "${case_body}" "${_meta_prefix}" _meta_idx REVERSE)
      if(_meta_idx GREATER_EQUAL 0 AND case_body MATCHES "\\]$")
        string(SUBSTRING "${case_body}" 0 ${_meta_idx} case_name)
        string(LENGTH "${case_body}" _case_body_len)
        math(EXPR _meta_value_idx "${_meta_idx} + ${_meta_prefix_len}")
        math(EXPR _meta_value_len "${_case_body_len} - ${_meta_value_idx} - 1")
        if(_meta_value_len GREATER_EQUAL 0)
          string(SUBSTRING "${case_body}" ${_meta_value_idx} ${_meta_value_len} case_meta)
        endif()
      endif()

      string(TOLOWER "${case_meta}" case_meta_lower)
      if(case_meta_lower MATCHES "(^|;)tags=([^;]*,)?death([,;]|$)" AND case_meta_lower MATCHES "(^|;)skip($|=)")
        continue()
      endif()
      if(case_meta_lower MATCHES "(^|;)tags=([^;]*,)?death([,;]|$)")
        list(APPEND _death_cases "${case_name}")
      else()
        list(APPEND _cases "${case_name}")
      endif()
    endforeach()
    set(${out_var} "${_cases}" PARENT_SCOPE)
    set(${out_death_var} "${_death_cases}" PARENT_SCOPE)
  endfunction()

  _gentest_parse_meta_list("${output}" normal_cases death_cases)

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
    if(NOT "${arg_DEATH_EXPECT_SUBSTRING}" STREQUAL "")
      set(expect_val "${arg_DEATH_EXPECT_SUBSTRING}")
      string(REPLACE ";" "\\;" expect_val "${expect_val}")
      set(expect_def "-DDEATH_EXPECT_SUBSTRING=${expect_val}")
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
    DEATH_EXPECT_SUBSTRING ${DEATH_EXPECT_SUBSTRING}
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
        DEATH_EXPECT_SUBSTRING)
    set(multi_value_args EXTRA_ARGS DISCOVERY_EXTRA_ARGS PROPERTIES)
    list(FIND ARGN "EXPECT_SUBSTRING" _gentest_legacy_expect_idx)
    list(FIND ARGN "DEATH_EXPECT_SUBSTRING" _gentest_death_expect_idx)
    if(_gentest_legacy_expect_idx GREATER_EQUAL 0 AND _gentest_death_expect_idx GREATER_EQUAL 0)
        message(FATAL_ERROR
            "gentest_discover_tests: do not pass both DEATH_EXPECT_SUBSTRING and deprecated EXPECT_SUBSTRING")
    endif()

    set(_gentest_discover_args ${ARGN})
    if(_gentest_legacy_expect_idx GREATER_EQUAL 0)
        list(REMOVE_AT _gentest_discover_args ${_gentest_legacy_expect_idx})
        list(INSERT _gentest_discover_args ${_gentest_legacy_expect_idx} DEATH_EXPECT_SUBSTRING)
    endif()

    cmake_parse_arguments(GENTEST "${options}" "${one_value_args}" "${multi_value_args}" ${_gentest_discover_args})

    if(_gentest_legacy_expect_idx GREATER_EQUAL 0)
        message(WARNING
            "gentest_discover_tests: EXPECT_SUBSTRING is deprecated; use DEATH_EXPECT_SUBSTRING instead. "
            "EXPECT_SUBSTRING only applies to death tests registered by gentest_discover_tests().")
    endif()

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
                -D "DEATH_EXPECT_SUBSTRING=${GENTEST_DEATH_EXPECT_SUBSTRING}"
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
            "      DEATH_EXPECT_SUBSTRING [==[${GENTEST_DEATH_EXPECT_SUBSTRING}]==]" "\n"
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
