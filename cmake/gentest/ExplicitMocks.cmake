include_guard(GLOBAL)

# Generated surfaces participate in native compiler and codegen dependency graphs.
# Preserve their timestamps when an unrelated configure step leaves them unchanged.
function(_gentest_write_explicit_mock_file_if_different path content)
    if(EXISTS "${path}")
        file(READ "${path}" _gentest_existing_content)
        if("${_gentest_existing_content}" STREQUAL "${content}")
            return()
        endif()
    endif()
    file(WRITE "${path}" "${content}")
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
            "gentest explicit mock module lookup does not support include-directory generator expressions other than "
            "BUILD_INTERFACE/INSTALL_INTERFACE. Unsupported root: '${raw_root}'")
    else()
        list(APPEND _gentest_roots "${raw_root}")
    endif()

    list(REMOVE_DUPLICATES _gentest_roots)
    set(${out_roots} "${_gentest_roots}" PARENT_SCOPE)
endfunction()

# Decides how consumers spell each DEFS file. A DEFS file is spelled relative
# to a base directory, and each base directory becomes a public include
# directory of the mock target and a base of the file set that installs DEFS.
#
# BASE_DIRS names the base directories explicitly. Without it, a DEFS file under
# CMAKE_CURRENT_BINARY_DIR is spelled relative to that directory and any other
# DEFS file relative to CMAKE_CURRENT_SOURCE_DIR, so 'a/mocks.hpp' keeps its
# directory and stays distinct from 'b/mocks.hpp' of another target. A base is
# never derived from the DEFS' common parent: for files in the source and build
# trees that would be the project root, the home directory or a filesystem root.
function(_gentest_resolve_explicit_mock_defs_layout target out_base_dirs out_includes)
    set(multi_value_args BASE_DIRS DEFS)
    cmake_parse_arguments(GENTEST_LAYOUT "" "" "${multi_value_args}" ${ARGN})

    set(_gentest_candidates "")
    set(_gentest_candidate_keys "")
    if(GENTEST_LAYOUT_BASE_DIRS)
        set(_gentest_explicit_bases TRUE)
        set(_gentest_candidate_inputs ${GENTEST_LAYOUT_BASE_DIRS})
    else()
        set(_gentest_explicit_bases FALSE)
        set(_gentest_candidate_inputs "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")
    endif()
    foreach(_gentest_candidate_input IN LISTS _gentest_candidate_inputs)
        if("${_gentest_candidate_input}" MATCHES "\\$<")
            message(FATAL_ERROR
                "gentest_add_mocks(${target}): generator-expression BASE_DIRS entries are not supported. "
                "Pass concrete directories instead: '${_gentest_candidate_input}'")
        endif()
        _gentest_normalize_path_and_key(
            "${_gentest_candidate_input}" "${CMAKE_CURRENT_SOURCE_DIR}" _gentest_candidate _gentest_candidate_key)
        cmake_path(GET _gentest_candidate ROOT_PATH _gentest_candidate_root)
        if(_gentest_candidate STREQUAL _gentest_candidate_root)
            message(FATAL_ERROR
                "gentest_add_mocks(${target}): base directory '${_gentest_candidate_input}' is a filesystem root. "
                "A base directory becomes a public include directory; pass BASE_DIRS <dir>... naming the directory "
                "DEFS are included from.")
        endif()
        string(REGEX REPLACE "/+$" "" _gentest_candidate "${_gentest_candidate}")
        string(REGEX REPLACE "/+$" "" _gentest_candidate_key "${_gentest_candidate_key}")
        if(NOT _gentest_candidate_key IN_LIST _gentest_candidate_keys)
            list(APPEND _gentest_candidates "${_gentest_candidate}")
            list(APPEND _gentest_candidate_keys "${_gentest_candidate_key}")
        endif()
    endforeach()

    set(_gentest_used_bases "")
    set(_gentest_used_keys "")
    set(_gentest_includes "")
    set(_gentest_include_keys "")
    set(_gentest_include_files "")
    foreach(_gentest_def IN LISTS GENTEST_LAYOUT_DEFS)
        _gentest_normalize_path_and_key("${_gentest_def}" "${CMAKE_CURRENT_SOURCE_DIR}" _gentest_def_abs _gentest_def_key)
        # The deepest base containing the file wins, so a build directory
        # inside the source directory still claims its own files.
        set(_gentest_best "")
        set(_gentest_best_key "")
        set(_gentest_best_length -1)
        foreach(_gentest_candidate _gentest_candidate_key IN ZIP_LISTS _gentest_candidates _gentest_candidate_keys)
            cmake_path(IS_PREFIX _gentest_candidate_key "${_gentest_def_key}" _gentest_is_prefix)
            string(LENGTH "${_gentest_candidate_key}" _gentest_candidate_length)
            if(_gentest_is_prefix AND _gentest_candidate_length GREATER _gentest_best_length)
                set(_gentest_best "${_gentest_candidate}")
                set(_gentest_best_key "${_gentest_candidate_key}")
                set(_gentest_best_length "${_gentest_candidate_length}")
            endif()
        endforeach()
        if(_gentest_best STREQUAL "")
            if(_gentest_explicit_bases)
                message(FATAL_ERROR
                    "gentest_add_mocks(${target}): DEFS file '${_gentest_def_abs}' is not inside any BASE_DIRS entry "
                    "(${_gentest_candidates}). Add the directory it is included from to BASE_DIRS.")
            endif()
            message(FATAL_ERROR
                "gentest_add_mocks(${target}): DEFS file '${_gentest_def_abs}' is outside CMAKE_CURRENT_SOURCE_DIR "
                "'${CMAKE_CURRENT_SOURCE_DIR}' and CMAKE_CURRENT_BINARY_DIR '${CMAKE_CURRENT_BINARY_DIR}', so its include "
                "path cannot be derived. Pass BASE_DIRS <dir>... naming the directory it is included from, for example "
                "the include root that already exposes it; that directory becomes a public include directory of "
                "'${target}'.")
        endif()
        file(RELATIVE_PATH _gentest_include "${_gentest_best}" "${_gentest_def_abs}")
        string(TOLOWER "${_gentest_include}" _gentest_include_key)
        list(FIND _gentest_include_keys "${_gentest_include_key}" _gentest_same_include_index)
        if(NOT _gentest_same_include_index EQUAL -1)
            list(GET _gentest_include_files ${_gentest_same_include_index} _gentest_same_include_file)
            if(NOT _gentest_same_include_file STREQUAL _gentest_def_abs)
                message(FATAL_ERROR
                    "gentest_add_mocks(${target}): DEFS files '${_gentest_same_include_file}' and '${_gentest_def_abs}' "
                    "would both be included as <${_gentest_include}>. Rename one, or pass BASE_DIRS that give them "
                    "different relative paths.")
            endif()
        endif()
        list(APPEND _gentest_includes "${_gentest_include}")
        list(APPEND _gentest_include_keys "${_gentest_include_key}")
        list(APPEND _gentest_include_files "${_gentest_def_abs}")
        if(NOT _gentest_best_key IN_LIST _gentest_used_keys)
            list(APPEND _gentest_used_bases "${_gentest_best}")
            list(APPEND _gentest_used_keys "${_gentest_best_key}")
        endif()
    endforeach()

    if(_gentest_explicit_bases)
        set(_gentest_used_bases "${_gentest_candidates}")
        set(_gentest_used_keys "${_gentest_candidate_keys}")
    endif()
    # CMake rejects nested base directories within a file set, and nested
    # include roots make '<x/y.hpp>' ambiguous.
    foreach(_gentest_outer _gentest_outer_key IN ZIP_LISTS _gentest_used_bases _gentest_used_keys)
        foreach(_gentest_inner _gentest_inner_key IN ZIP_LISTS _gentest_used_bases _gentest_used_keys)
            if(_gentest_outer_key STREQUAL _gentest_inner_key)
                continue()
            endif()
            cmake_path(IS_PREFIX _gentest_outer_key "${_gentest_inner_key}" _gentest_is_nested)
            if(NOT _gentest_is_nested)
                continue()
            endif()
            if(_gentest_explicit_bases)
                message(FATAL_ERROR
                    "gentest_add_mocks(${target}): BASE_DIRS entry '${_gentest_inner}' is inside '${_gentest_outer}'. "
                    "Base directories must not contain each other.")
            endif()
            message(FATAL_ERROR
                "gentest_add_mocks(${target}): DEFS come from both CMAKE_CURRENT_SOURCE_DIR and "
                "CMAKE_CURRENT_BINARY_DIR, and '${_gentest_inner}' is inside '${_gentest_outer}'. Pass BASE_DIRS "
                "<dir>... naming separate directories for the authored and the generated DEFS.")
        endforeach()
    endforeach()

    set(${out_base_dirs} "${_gentest_used_bases}" PARENT_SCOPE)
    set(${out_includes} "${_gentest_includes}" PARENT_SCOPE)
endfunction()

# Fails when two explicit mock targets reachable from one consumer include
# different DEFS files by the same spelling: the compiler would resolve both
# includes to whichever base directory comes first and silently drop the other
# target's declarations.
function(_gentest_check_explicit_mock_defs_includes consumer)
    set(_gentest_seen_keys "")
    set(_gentest_seen_files "")
    set(_gentest_seen_owners "")
    set(_gentest_mock_targets ${ARGN})
    list(REMOVE_DUPLICATES _gentest_mock_targets)
    foreach(_gentest_mock_target IN LISTS _gentest_mock_targets)
        if(NOT TARGET "${_gentest_mock_target}")
            continue()
        endif()
        get_target_property(_gentest_mock_includes "${_gentest_mock_target}" GENTEST_EXPLICIT_MOCK_DEFS_INCLUDES)
        get_target_property(_gentest_mock_files "${_gentest_mock_target}" GENTEST_EXPLICIT_MOCK_DEFS_FILES)
        if(NOT _gentest_mock_includes OR NOT _gentest_mock_files)
            continue()
        endif()
        foreach(_gentest_include _gentest_file IN ZIP_LISTS _gentest_mock_includes _gentest_mock_files)
            string(TOLOWER "${_gentest_include}" _gentest_include_key)
            list(FIND _gentest_seen_keys "${_gentest_include_key}" _gentest_seen_index)
            if(_gentest_seen_index EQUAL -1)
                list(APPEND _gentest_seen_keys "${_gentest_include_key}")
                list(APPEND _gentest_seen_files "${_gentest_file}")
                list(APPEND _gentest_seen_owners "${_gentest_mock_target}")
                continue()
            endif()
            list(GET _gentest_seen_files ${_gentest_seen_index} _gentest_seen_file)
            list(GET _gentest_seen_owners ${_gentest_seen_index} _gentest_seen_owner)
            if(NOT _gentest_seen_file STREQUAL _gentest_file)
                message(FATAL_ERROR
                    "gentest: '${consumer}' uses explicit mock targets '${_gentest_seen_owner}' and "
                    "'${_gentest_mock_target}', which both include a DEFS file as <${_gentest_include}>:\n"
                    "  ${_gentest_seen_file}\n"
                    "  ${_gentest_file}\n"
                    "Only one of them would be found. Give the targets different DEFS include paths with the BASE_DIRS "
                    "argument of gentest_add_mocks(), or rename one of the files.")
            endif()
        endforeach()
    endforeach()
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
    set(one_value_args OUTPUT_DIR MODULE_NAME HEADER_NAME BACKEND)
    set(multi_value_args DEFS BASE_DIRS CLANG_ARGS DEPENDS LINK_LIBRARIES)
    cmake_parse_arguments(GENTEST "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(TARGET ${target})
        message(FATAL_ERROR "gentest_add_mocks(${target}): target already exists")
    endif()
    if(NOT GENTEST_DEFS)
        message(FATAL_ERROR "gentest_add_mocks(${target}): DEFS is required")
    endif()
    _gentest_make_codegen_target_id("${target}" _gentest_target_id)
    _gentest_normalize_mock_backend("${GENTEST_BACKEND}" "gentest_add_mocks(): BACKEND" _gentest_mock_backend)
    _gentest_is_third_party_mock_backend("${_gentest_mock_backend}" _gentest_uses_third_party_mock_backend)

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
    _gentest_normalize_path_and_key("${_gentest_output_dir}" "${CMAKE_CURRENT_BINARY_DIR}" _gentest_output_root _gentest_output_dir_key)
    _gentest_reserve_unique_owner("GENTEST_EXPLICIT_MOCK_OUTDIR_OWNER" "${_gentest_output_dir_key}" "${target}" _gentest_prev_owner)
    if(_gentest_prev_owner AND NOT _gentest_prev_owner STREQUAL "${target}")
        message(FATAL_ERROR
            "gentest_add_mocks(${target}): OUTPUT_DIR '${_gentest_output_root}' is already used by '${_gentest_prev_owner}'. "
            "Each explicit mock target must have a unique OUTPUT_DIR.")
    endif()
    _gentest_configure_output_dir("${_gentest_output_root}" _gentest_output_dir)
    if(CMAKE_CONFIGURATION_TYPES)
        set(_gentest_staging_dir "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/gentest_mocks/${_gentest_target_id}")
    else()
        set(_gentest_staging_dir "${_gentest_output_dir}")
    endif()
    file(MAKE_DIRECTORY "${_gentest_output_root}" "${_gentest_staging_dir}")

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
    if(_gentest_module_defs AND _gentest_uses_third_party_mock_backend)
        message(FATAL_ERROR
            "gentest_add_mocks(${target}): BACKEND ${_gentest_mock_backend} supports textual DEFS only. "
            "Named-module mock DEFS currently require BACKEND gentest.")
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
        _gentest_normalize_path_and_key(
            "${_gentest_public_header_name}"
            "${_gentest_output_root}"
            _gentest_public_header_at_root
            _gentest_public_header_key)
        file(RELATIVE_PATH _gentest_public_header_rel_from_output "${_gentest_output_root}" "${_gentest_public_header_at_root}")
        if(_gentest_public_header_rel_from_output MATCHES "^\\.\\.")
            message(FATAL_ERROR
                "gentest_add_mocks(${target}): HEADER_NAME must stay within OUTPUT_DIR '${_gentest_output_root}'. "
                "Got '${_gentest_public_header_at_root}'.")
        endif()
        set(_gentest_public_header "${_gentest_output_dir}/${_gentest_public_header_rel_from_output}")
        get_filename_component(_gentest_public_header_dir "${_gentest_public_header}" DIRECTORY)
        set(_gentest_reserved_output_paths
            "${_gentest_output_root}/${_gentest_target_id}_anchor.cpp"
            "${_gentest_output_root}/${_gentest_target_id}_defs.cpp"
            "${_gentest_output_root}/${_gentest_target_id}_mock_registry.hpp"
            "${_gentest_output_root}/${_gentest_target_id}_mock_impl.hpp"
            "${_gentest_output_root}/${_gentest_target_id}.cppm")
        foreach(_gentest_reserved_output IN LISTS _gentest_reserved_output_paths)
            _gentest_normalize_path_and_key(
                "${_gentest_reserved_output}"
                "${_gentest_output_root}"
                _gentest_reserved_output_abs
                _gentest_reserved_output_key)
            if(_gentest_public_header_key STREQUAL _gentest_reserved_output_key)
                message(FATAL_ERROR
                    "gentest_add_mocks(${target}): HEADER_NAME '${_gentest_public_header_name}' collides with a reserved generated output "
                    "'${_gentest_reserved_output_abs}'. Choose a different HEADER_NAME.")
            endif()
        endforeach()
    endif()

    # DEFS are used in place, as ordinary public headers or module units of the
    # mock target, and are never copied. A consumer that includes the same
    # production headers therefore sees one file, so include guards and
    # #pragma once both hold. Whatever DEFS include must be reachable by
    # consumers, like any public header: through LINK_LIBRARIES or by adding the
    # header to this target's DEFS file set.
    _gentest_resolve_explicit_mock_defs_layout(${target} _gentest_defs_base_dirs _gentest_defs_includes
        BASE_DIRS ${GENTEST_BASE_DIRS}
        DEFS ${_gentest_textual_defs} ${_gentest_module_defs})
    set(_gentest_materialized_module_defs "${_gentest_module_defs}")

    set(_gentest_codegen_sources "${_gentest_materialized_module_defs}")

    if(_gentest_uses_third_party_mock_backend)
        set(_gentest_mock_api_header "gentest/mock_fwd.h")
    else()
        set(_gentest_mock_api_header "gentest/mock.h")
    endif()

    if(_gentest_textual_defs)
        set(_gentest_public_header_content
"// This file is auto-generated by gentest (explicit mocks surface).\n\
// Do not edit manually.\n\
\n\
#pragma once\n\
\n")
        if(NOT _gentest_uses_third_party_mock_backend)
            string(APPEND _gentest_public_header_content "#define GENTEST_NO_AUTO_MOCK_INCLUDE 1\n")
            string(APPEND _gentest_public_header_content "#include \"${_gentest_mock_api_header}\"\n")
            foreach(_gentest_def _gentest_public_def_include IN ZIP_LISTS _gentest_textual_defs _gentest_defs_includes)
                # Resolved through a DEFS base directory, in the build tree and
                # after installation. Angle brackets skip this header's own
                # directory, so a DEFS file sharing its name is still found.
                string(TOLOWER "${_gentest_public_def_include}" _gentest_public_def_include_key)
                string(TOLOWER "${_gentest_public_header_rel_from_output}" _gentest_public_header_include_key)
                if(_gentest_public_def_include_key STREQUAL _gentest_public_header_include_key)
                    message(FATAL_ERROR
                        "gentest_add_mocks(${target}): HEADER_NAME '${_gentest_public_header_rel_from_output}' has the same "
                        "include path as DEFS file '${_gentest_def}'. Choose a different HEADER_NAME.")
                endif()
                # Two mock targets that spell different DEFS files alike would
                # both resolve to the first one on the include path, which
                # #pragma once then hides. Configure-time checks cover consumers
                # that use gentest_attach_codegen() or gentest_link_mocks();
                # this covers every other consumer, and installed packages.
                string(MD5 _gentest_public_def_include_md5 "${_gentest_public_def_include_key}")
                string(TOUPPER "${_gentest_public_def_include_md5}" _gentest_public_def_include_md5)
                set(_gentest_public_def_include_macro "GENTEST_EXPLICIT_MOCK_DEFS_${_gentest_public_def_include_md5}")
                string(APPEND _gentest_public_header_content
                    "#ifdef ${_gentest_public_def_include_macro}\n"
                    "#error \"gentest: another explicit mock target already includes a different DEFS file as "
                    "<${_gentest_public_def_include}>; give '${target}' a distinct DEFS include path with "
                    "gentest_add_mocks(BASE_DIRS ...)\"\n"
                    "#endif\n"
                    "#define ${_gentest_public_def_include_macro} 1\n"
                    "#include <${_gentest_public_def_include}>\n")
            endforeach()
            string(APPEND _gentest_public_header_content "#undef GENTEST_NO_AUTO_MOCK_INCLUDE\n")
        endif()
        string(APPEND _gentest_public_header_content "\n")
        file(RELATIVE_PATH _gentest_public_registry_include "${_gentest_public_header_dir}" "${_gentest_output_dir}/${_gentest_target_id}_mock_registry.hpp")
        file(RELATIVE_PATH _gentest_public_impl_include "${_gentest_public_header_dir}" "${_gentest_output_dir}/${_gentest_target_id}_mock_impl.hpp")
        string(APPEND _gentest_public_header_content "#include \"${_gentest_public_registry_include}\"\n")
        string(APPEND _gentest_public_header_content "#include \"${_gentest_public_impl_include}\"\n")
        if(CMAKE_CONFIGURATION_TYPES)
            file(GENERATE OUTPUT "${_gentest_public_header}" CONTENT "${_gentest_public_header_content}")
        else()
            file(MAKE_DIRECTORY "${_gentest_public_header_dir}")
            _gentest_write_explicit_mock_file_if_different("${_gentest_public_header}" "${_gentest_public_header_content}")
        endif()
    endif()

    set(_gentest_textual_wrapper "${_gentest_staging_dir}/${_gentest_target_id}_defs.cpp")
    if(_gentest_textual_defs)
        set(_gentest_textual_wrapper_content
"// This file is auto-generated by gentest (explicit mocks surface).\n\
// Do not edit manually.\n\
\n\
#include \"${_gentest_mock_api_header}\"\n")
        if(_gentest_uses_third_party_mock_backend)
            string(APPEND _gentest_textual_wrapper_content "#define GENTEST_NO_AUTO_MOCK_INCLUDE 1\n")
            string(APPEND _gentest_textual_wrapper_content "#define GENTEST_NO_EXPECT_CALL_MACROS 1\n")
            string(APPEND _gentest_textual_wrapper_content "#define GENTEST_MOCK_FORWARD_ONLY 1\n")
        endif()
        foreach(_gentest_def IN LISTS _gentest_textual_defs)
            # This scan-only wrapper is never installed; include the DEFS file
            # itself so codegen sees the authored declarations in place.
            string(APPEND _gentest_textual_wrapper_content "#include \"${_gentest_def}\"\n")
        endforeach()
        if(_gentest_uses_third_party_mock_backend)
            string(APPEND _gentest_textual_wrapper_content "#undef GENTEST_MOCK_FORWARD_ONLY\n")
            string(APPEND _gentest_textual_wrapper_content "#undef GENTEST_NO_EXPECT_CALL_MACROS\n")
            string(APPEND _gentest_textual_wrapper_content "#undef GENTEST_NO_AUTO_MOCK_INCLUDE\n")
        endif()
        _gentest_write_explicit_mock_file_if_different("${_gentest_textual_wrapper}" "${_gentest_textual_wrapper_content}")
        list(APPEND _gentest_codegen_sources "${_gentest_textual_wrapper}")
    endif()

    set(_gentest_anchor_cpp "${_gentest_output_dir}/${_gentest_target_id}_anchor.cpp")
    if(NOT _gentest_uses_third_party_mock_backend)
        set(_gentest_anchor_content
"// This file is auto-generated by gentest (explicit mocks anchor).\n\
// Do not edit manually.\n\
\n\
namespace gentest::detail {\n\
int ${_gentest_target_id}_explicit_mock_anchor = 0;\n\
} // namespace gentest::detail\n")
    else()
        set(_gentest_anchor_content
"// This file is auto-generated by gentest (explicit mocks anchor).\n\
// Do not edit manually.\n\
\n\
namespace {\n\
[[maybe_unused]] int ${_gentest_target_id}_explicit_mock_anchor = 0;\n\
}\n")
    endif()
    if(CMAKE_CONFIGURATION_TYPES)
        file(GENERATE OUTPUT "${_gentest_anchor_cpp}" CONTENT "${_gentest_anchor_content}")
        set_source_files_properties("${_gentest_anchor_cpp}" PROPERTIES GENERATED TRUE)
    else()
        _gentest_write_explicit_mock_file_if_different("${_gentest_anchor_cpp}" "${_gentest_anchor_content}")
    endif()

    add_library(${target} STATIC)
    set_target_properties(${target} PROPERTIES
        GENTEST_EXPLICIT_MOCK_TARGET TRUE
        GENTEST_CODEGEN_TARGET_ID "${_gentest_target_id}")
    _gentest_append_target_export_property(${target} GENTEST_EXPLICIT_MOCK_TARGET)
    _gentest_append_target_export_property(${target} GENTEST_CODEGEN_TARGET_ID)
    target_compile_features(${target} PUBLIC cxx_std_20)
    if(NOT _gentest_uses_third_party_mock_backend)
        target_link_libraries(${target} PUBLIC gentest::gentest)
    else()
        get_target_property(_gentest_public_include_dirs gentest::gentest INTERFACE_INCLUDE_DIRECTORIES)
        if(_gentest_public_include_dirs AND NOT _gentest_public_include_dirs MATCHES "-NOTFOUND$")
            target_include_directories(${target} PRIVATE ${_gentest_public_include_dirs})
        endif()
    endif()
    if(GENTEST_LINK_LIBRARIES)
        target_link_libraries(${target} PUBLIC ${GENTEST_LINK_LIBRARIES})
    endif()
    if(_gentest_textual_defs AND NOT _gentest_uses_third_party_mock_backend)
        # Read by _gentest_check_explicit_mock_defs_includes().
        set_target_properties(${target} PROPERTIES
            GENTEST_EXPLICIT_MOCK_DEFS_INCLUDES "${_gentest_defs_includes}"
            GENTEST_EXPLICIT_MOCK_DEFS_FILES "${_gentest_textual_defs}")
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
                    BASE_DIRS ${_gentest_defs_base_dirs}
                    FILES ${_gentest_materialized_module_defs})
        # Module units can include headers beside them in their global module
        # fragment; keep those reachable like a textual DEFS root.
        foreach(_gentest_defs_base_dir IN LISTS _gentest_defs_base_dirs)
            target_include_directories(${target} PUBLIC "$<BUILD_INTERFACE:${_gentest_defs_base_dir}>")
        endforeach()
    endif()

    set(_gentest_aggregate_module "")
    set(_gentest_aggregate_module_output "")
    if(NOT "${GENTEST_MODULE_NAME}" STREQUAL "")
        set(_gentest_aggregate_module_rel "${GENTEST_MODULE_NAME}")
        string(REPLACE "." "/" _gentest_aggregate_module_rel "${_gentest_aggregate_module_rel}")
        string(REPLACE ":" "/" _gentest_aggregate_module_rel "${_gentest_aggregate_module_rel}")
        set(_gentest_aggregate_module "${_gentest_output_root}/${_gentest_aggregate_module_rel}.cppm")
        set(_gentest_aggregate_module_output "${_gentest_output_dir}/${_gentest_aggregate_module_rel}.cppm")
    endif()

    gentest_attach_codegen(${target}
        OUTPUT_DIR "${_gentest_output_root}"
        SOURCES ${_gentest_codegen_sources}
        CLANG_ARGS ${GENTEST_CLANG_ARGS}
        DEPENDS ${GENTEST_DEPENDS}
        MOCK_BACKEND "${_gentest_mock_backend}"
        MOCK_AGGREGATE_MODULE_NAME "${GENTEST_MODULE_NAME}"
        MOCK_AGGREGATE_MODULE_OUTPUT "${_gentest_aggregate_module}"
        MOCK_INCLUDE_ROOTS
            ${_gentest_defs_base_dirs}
            "$<TARGET_PROPERTY:${target},INTERFACE_INCLUDE_DIRECTORIES>")

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
                        "${_gentest_mock_registry}"
                        "${_gentest_mock_impl}"
                        "${_gentest_mock_registry_header_domain}"
                        "${_gentest_mock_impl_header_domain}")
        # A separate file set: its base directories are where DEFS are
        # authored or generated, which may contain or sit inside OUTPUT_DIR.
        target_sources(${target}
            PUBLIC
                FILE_SET gentest_explicit_mock_defs
                    TYPE HEADERS
                    BASE_DIRS ${_gentest_defs_base_dirs}
                    FILES ${_gentest_textual_defs})
    endif()

    set(_gentest_module_support_headers "")
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
        set_source_files_properties("${_gentest_aggregate_module_output}" PROPERTIES GENERATED TRUE)
        target_sources(${target}
            PUBLIC
                FILE_SET gentest_explicit_mock_aggregate_module
                    TYPE CXX_MODULES
                    BASE_DIRS "${_gentest_output_dir}"
                    FILES "${_gentest_aggregate_module_output}")
        file(RELATIVE_PATH
            _gentest_aggregate_module_rel_from_output
            "${_gentest_output_dir}"
            "${_gentest_aggregate_module_output}")
        _gentest_append_target_list_property(
            ${target}
            GENTEST_EXPLICIT_MOCK_MODULE_BUILD_SOURCES
            "${GENTEST_MODULE_NAME}=${_gentest_aggregate_module_output}")
        _gentest_append_target_list_property(
            ${target}
            GENTEST_EXPLICIT_MOCK_MODULE_REL_SOURCES
            "${GENTEST_MODULE_NAME}=${_gentest_aggregate_module_rel_from_output}")
        _gentest_append_target_export_property(${target} GENTEST_EXPLICIT_MOCK_MODULE_REL_SOURCES)
    endif()
endfunction()
