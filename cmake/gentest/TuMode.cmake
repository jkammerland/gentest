include_guard(GLOBAL)

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
        TARGET TARGET_ID OUTPUT_DIR
        OUT_OUTPUT_DIR OUT_WRAPPER_CPP OUT_WRAPPER_HEADERS OUT_EXTRA_CPP
        OUT_ARTIFACT_MANIFEST OUT_COMPILE_CONTEXT_IDS OUT_ARTIFACT_OWNER_SOURCES)
    set(multi_value_args TUS TU_SOURCE_ENTRIES MODULE_NAMES NEEDS_MODULE_SCAN)
    cmake_parse_arguments(GENTEST "" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(GENTEST_OUTPUT_DIR)
        set(_gentest_output_dir "${GENTEST_OUTPUT_DIR}")
    else()
        set(_gentest_output_dir "${CMAKE_CURRENT_BINARY_DIR}/gentest/${GENTEST_TARGET_ID}")
    endif()

    if("${_gentest_output_dir}" MATCHES "\\$<")
        message(FATAL_ERROR
            "gentest_attach_codegen(${GENTEST_TARGET}): OUTPUT_DIR contains generator expressions, which is not supported in TU wrapper mode "
            "(requires a concrete directory to generate shim translation units). "
            "Pass a concrete OUTPUT_DIR.")
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
    foreach(_gentest_removed_arg IN LISTS ARGN)
        if(_gentest_removed_arg STREQUAL "OUTPUT")
            message(FATAL_ERROR
                "gentest_attach_codegen(${target}): OUTPUT manifest/single-TU mode was removed in gentest 2.0.0. "
                "Omit OUTPUT to use TU-wrapper mode, or pass OUTPUT_DIR for a concrete generated-output directory.")
        endif()
        if(_gentest_removed_arg STREQUAL "NO_INCLUDE_SOURCES")
            message(FATAL_ERROR
                "gentest_attach_codegen(${target}): NO_INCLUDE_SOURCES was removed with legacy manifest mode in gentest 2.0.0. "
                "Use TU-wrapper mode so owner sources stay in normal compilation units.")
        endif()
    endforeach()

    set(options STRICT_FIXTURE QUIET_CLANG MODULE_REGISTRATION)
    set(one_value_args OUTPUT_DIR ENTRY FILE_SET)
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
    # - Otherwise, emit a wrapper TU + header per translation unit and replace
    #   the target sources (gtest/catch/doctest-like workflow).
    set(_gentest_mode "tu")
    if(GENTEST_MODULE_REGISTRATION)
        set(_gentest_mode "module_registration")
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

    set(_gentest_wrapper_cpp "")
    set(_gentest_wrapper_headers "")
    set(_gentest_extra_cpp "")
    set(_gentest_artifact_manifest "")
    set(_gentest_compile_context_ids "")
    set(_gentest_artifact_owner_sources "")
    set(_gentest_tu_manifest_enabled FALSE)
    if(_gentest_mode STREQUAL "module_registration")
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

    if(_gentest_mode STREQUAL "module_registration")
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
    if(_gentest_attach_discovers_mocks AND NOT _gentest_mode STREQUAL "module_registration")
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

    if(_gentest_mode STREQUAL "module_registration")
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

    if(_gentest_mode STREQUAL "module_registration")
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

    # MODULE_REGISTRATION consumes the split inspect-mocks manifest directly;
    # only legacy TU wrapper-mode explicit mock targets publish module wrappers.
    if(_gentest_mode STREQUAL "tu" AND _gentest_attach_discovers_mocks AND _gentest_module_wrapper_outputs)
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
