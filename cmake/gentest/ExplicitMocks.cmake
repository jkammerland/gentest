include_guard(GLOBAL)

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

    list(REMOVE_DUPLICATES _gentest_mappings)
    set(${out_mappings} "${_gentest_mappings}" PARENT_SCOPE)
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
    elseif(NOT "${GENTEST_MODULE_NAME}" STREQUAL "")
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

    set(_gentest_aggregate_module "")
    if(NOT "${GENTEST_MODULE_NAME}" STREQUAL "")
        set(_gentest_aggregate_module_rel "${GENTEST_MODULE_NAME}")
        string(REPLACE "." "/" _gentest_aggregate_module_rel "${_gentest_aggregate_module_rel}")
        string(REPLACE ":" "/" _gentest_aggregate_module_rel "${_gentest_aggregate_module_rel}")
        set(_gentest_aggregate_module "${_gentest_output_dir}/${_gentest_aggregate_module_rel}.cppm")
    endif()

    gentest_attach_codegen(${target}
        OUTPUT_DIR "${_gentest_output_dir}"
        SOURCES ${_gentest_codegen_sources}
        CLANG_ARGS ${GENTEST_CLANG_ARGS}
        DEPENDS ${GENTEST_DEPENDS}
        MOCK_AGGREGATE_MODULE_NAME "${GENTEST_MODULE_NAME}"
        MOCK_AGGREGATE_MODULE_OUTPUT "${_gentest_aggregate_module}")

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

    if(NOT "${GENTEST_MODULE_NAME}" STREQUAL "")
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
