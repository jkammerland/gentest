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

function(_gentest_resolve_codegen_backend)
    set(one_value_args TARGET OUT_CODEGEN_TARGET OUT_CODEGEN_EXECUTABLE)
    cmake_parse_arguments(GENTEST "" "${one_value_args}" "" ${ARGN})

    set(_gentest_codegen_target "")
    set(_gentest_codegen_executable "")
    if(CMAKE_CROSSCOMPILING AND NOT GENTEST_CODEGEN_EXECUTABLE AND NOT GENTEST_CODEGEN_TARGET)
        message(FATAL_ERROR
            "gentest_attach_codegen(${GENTEST_TARGET}): cross-compiling requires a host gentest_codegen. "
            "Set -DGENTEST_CODEGEN_EXECUTABLE=<path> or -DGENTEST_CODEGEN_TARGET=<target>.")
    endif()
    if(GENTEST_CODEGEN_EXECUTABLE)
        if(NOT EXISTS "${GENTEST_CODEGEN_EXECUTABLE}" OR IS_DIRECTORY "${GENTEST_CODEGEN_EXECUTABLE}")
            message(FATAL_ERROR
                "gentest_attach_codegen(${GENTEST_TARGET}): GENTEST_CODEGEN_EXECUTABLE='${GENTEST_CODEGEN_EXECUTABLE}' does not exist "
                "or is not a file")
        endif()
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

    set(${GENTEST_OUT_CODEGEN_TARGET} "${_gentest_codegen_target}" PARENT_SCOPE)
    set(${GENTEST_OUT_CODEGEN_EXECUTABLE} "${_gentest_codegen_executable}" PARENT_SCOPE)
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

function(_gentest_extract_module_name input out_name)
    file(STRINGS "${input}" _gentest_module_lines)

    set(_gentest_module_name "")
    foreach(_gentest_line IN LISTS _gentest_module_lines)
        string(REGEX REPLACE "//.*$" "" _gentest_candidate "${_gentest_line}")
        string(STRIP "${_gentest_candidate}" _gentest_candidate)
        if(_gentest_candidate STREQUAL "")
            continue()
        endif()
        if(_gentest_candidate MATCHES "^(export[ \t]+)?module[ \t]*;$")
            continue()
        endif()
        if(_gentest_candidate MATCHES "^(export[ \t]+)?module[ \t]+([^; \t]+)[ \t]*;")
            set(_gentest_module_name "${CMAKE_MATCH_2}")
            break()
        endif()
    endforeach()

    if(_gentest_module_name STREQUAL "")
        message(FATAL_ERROR
            "gentest_attach_codegen: unable to determine module name for '${input}'. "
            "Expected a named module declaration like 'export module name;' or 'module name;'.")
    endif()

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

function(_gentest_copy_source_properties_to_wrappers)
    set(multi_value_args TU_SOURCE_ENTRIES TUS WRAPPER_CPP EXTRA_CPP)
    cmake_parse_arguments(GENTEST "" "" "${multi_value_args}" ${ARGN})

    set(_gentest_source_props COMPILE_DEFINITIONS COMPILE_OPTIONS INCLUDE_DIRECTORIES COMPILE_FLAGS)
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
    set(one_value_args TARGET TARGET_ID OUTPUT_DIR NO_INCLUDE_SOURCES OUT_OUTPUT_DIR OUT_WRAPPER_CPP OUT_WRAPPER_HEADERS OUT_EXTRA_CPP)
    set(multi_value_args TUS TU_SOURCE_ENTRIES MODULE_NAMES)
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
            list(APPEND _gentest_registration_cpp "${_gentest_output_dir}/tu_${_idx_str}_${_stem}.gentest.cpp")
        endif()
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
#include \"${_rel_src}\"\n\
\n\
// Include generated registrations after the original TU is visible.\n\
// During codegen or module-dependency scans, this header may not exist yet.\n\
#if !defined(GENTEST_CODEGEN) && __has_include(\"${_wrap_header_name}\")\n\
#include \"${_wrap_header_name}\"\n\
#endif\n")

            file(GENERATE OUTPUT "${_wrap_cpp}" CONTENT "${_shim_content}")
            set_source_files_properties("${_wrap_cpp}" PROPERTIES OBJECT_DEPENDS "${_wrap_header}")
        else()
            set(_shim_content
"// This file is auto-generated by gentest (CMake shim).\n\
// Do not edit manually.\n\
\n\
module;\n\
\n\
// Pull standard/gentest headers in through the global module fragment so the\n\
// generated registration body can use them without textually re-entering them\n\
// after the named module is attached.\n\
#include <array>\n\
#include <span>\n\
#include <type_traits>\n\
\n\
#include \"gentest/runner.h\"\n\
#include \"gentest/fixture.h\"\n\
\n\
// Attach the shim as a module implementation unit so generated wrappers can\n\
// reference the named module's declarations without a second import step.\n\
module ${_module_name};\n\
\n\
// Include generated registrations after the module import is visible.\n\
// During codegen or module-dependency scans, this header may not exist yet.\n\
#if !defined(GENTEST_CODEGEN) && __has_include(\"${_wrap_header_name}\")\n\
#define GENTEST_TU_REGISTRATION_HEADER_NO_PREAMBLE 1\n\
#include \"${_wrap_header_name}\"\n\
#undef GENTEST_TU_REGISTRATION_HEADER_NO_PREAMBLE\n\
#endif\n")

            if(NOT _reg_cpp STREQUAL "__gentest_no_registration__")
                file(GENERATE OUTPUT "${_reg_cpp}" CONTENT "${_shim_content}")
                set_source_files_properties("${_reg_cpp}" PROPERTIES OBJECT_DEPENDS "${_wrap_header}")
                list(APPEND _gentest_extra_cpp "${_reg_cpp}")
                list(APPEND _gentest_module_generated_sources "${_wrap_cpp}" "${_reg_cpp}")
            endif()
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
    _gentest_copy_source_properties_to_wrappers(
        TU_SOURCE_ENTRIES ${GENTEST_TU_SOURCE_ENTRIES}
        TUS ${GENTEST_TUS}
        WRAPPER_CPP ${_gentest_wrapper_cpp}
        EXTRA_CPP ${_gentest_registration_cpp})

    set(${GENTEST_OUT_OUTPUT_DIR} "${_gentest_output_dir}" PARENT_SCOPE)
    set(${GENTEST_OUT_WRAPPER_CPP} "${_gentest_wrapper_cpp}" PARENT_SCOPE)
    set(${GENTEST_OUT_WRAPPER_HEADERS} "${_gentest_wrapper_headers}" PARENT_SCOPE)
    set(${GENTEST_OUT_EXTRA_CPP} "${_gentest_extra_cpp}" PARENT_SCOPE)
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
        list(LENGTH _gentest_module_wrappers _gentest_module_wrapper_count)
        math(EXPR _gentest_last_module_wrapper "${_gentest_module_wrapper_count} - 1")
        foreach(_idx RANGE 0 ${_gentest_last_module_wrapper})
            list(GET _gentest_module_tus ${_idx} _module_tu)
            list(GET _gentest_module_wrappers ${_idx} _module_wrap_cpp)
            _gentest_normalize_path_and_key("${_module_tu}" "${CMAKE_CURRENT_SOURCE_DIR}" _module_tu_abs _module_tu_key)
            list(FIND _gentest_module_keys_in_sets "${_module_tu_key}" _module_in_set_idx)
            if(_module_in_set_idx EQUAL -1)
                list(APPEND _gentest_new_sources "${_module_wrap_cpp}")
            endif()
        endforeach()
    endif()

    set_property(TARGET ${GENTEST_TARGET} PROPERTY SOURCES "${_gentest_new_sources}")

    add_custom_target(gentest_codegen_${GENTEST_TARGET_ID} DEPENDS ${GENTEST_CODEGEN_OUTPUTS})
    if(TARGET gentest_codegen_all)
        add_dependencies(gentest_codegen_all gentest_codegen_${GENTEST_TARGET_ID})
    endif()
    add_dependencies(${GENTEST_TARGET} gentest_codegen_${GENTEST_TARGET_ID})
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

    # Scan sources: explicit SOURCES preferred, otherwise pull from target and
    # any named module file sets attached to it.
    set(_gentest_scan_sources "${GENTEST_SOURCES}")
    if(NOT _gentest_scan_sources)
        get_target_property(_gentest_scan_sources ${target} SOURCES)

        get_target_property(_gentest_module_sets ${target} CXX_MODULE_SETS)
        if(NOT _gentest_module_sets STREQUAL "NOTFOUND")
            foreach(_gentest_module_set IN LISTS _gentest_module_sets)
                get_target_property(_gentest_module_files ${target} CXX_MODULE_SET_${_gentest_module_set})
                if(NOT _gentest_module_files STREQUAL "NOTFOUND")
                    list(APPEND _gentest_scan_sources ${_gentest_module_files})
                endif()
            endforeach()
        endif()
    endif()
    if(NOT _gentest_scan_sources)
        message(FATAL_ERROR "gentest_attach_codegen(${target}): SOURCES not provided and target has no SOURCES property")
    endif()

    # Select translation units and named module interface units (no generator expressions).
    set(_gentest_tus "")
    set(_gentest_tu_source_entries "")
    set(_gentest_module_names "")
    set(_gentest_skipped_genex_sources "")
    foreach(_gentest_src IN LISTS _gentest_scan_sources)
        if("${_gentest_src}" MATCHES "\\$<")
            list(APPEND _gentest_skipped_genex_sources "${_gentest_src}")
            continue()
        endif()
        get_filename_component(_gentest_ext "${_gentest_src}" EXT)
        set(_gentest_is_module FALSE)
        if(_gentest_ext MATCHES "^\\.(cppm|ccm|cxxm|ixx|mxx)$")
            set(_gentest_is_module TRUE)
        elseif(NOT _gentest_ext MATCHES "^\\.(cc|cpp|cxx)$")
            continue()
        endif()
        _gentest_normalize_path_and_key("${_gentest_src}" "${CMAKE_CURRENT_SOURCE_DIR}" _gentest_src_abs _gentest_src_key)
        list(APPEND _gentest_tu_source_entries "${_gentest_src}")
        list(APPEND _gentest_tus "${_gentest_src_abs}")
        if(_gentest_is_module)
            _gentest_extract_module_name("${_gentest_src_abs}" _gentest_module_name)
            list(APPEND _gentest_module_names "${_gentest_module_name}")
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

    set(_gentest_manifest_output "")
    set(_gentest_wrapper_cpp "")
    set(_gentest_wrapper_headers "")
    set(_gentest_extra_cpp "")
    if(_gentest_mode STREQUAL "manifest")
        _gentest_configure_manifest_mode(
            TARGET ${target}
            TARGET_ID ${_gentest_target_id}
            OUTPUT "${GENTEST_OUTPUT}"
            TUS ${_gentest_tus}
            OUT_OUTPUT _gentest_manifest_output
            OUT_OUTPUT_DIR _gentest_output_dir)
    else()
        _gentest_prepare_tu_mode(
            TARGET ${target}
            TARGET_ID ${_gentest_target_id}
            OUTPUT_DIR "${GENTEST_OUTPUT_DIR}"
            NO_INCLUDE_SOURCES "${GENTEST_NO_INCLUDE_SOURCES}"
            TUS ${_gentest_tus}
            TU_SOURCE_ENTRIES ${_gentest_tu_source_entries}
            MODULE_NAMES ${_gentest_module_names}
            OUT_OUTPUT_DIR _gentest_output_dir
            OUT_WRAPPER_CPP _gentest_wrapper_cpp
            OUT_WRAPPER_HEADERS _gentest_wrapper_headers
            OUT_EXTRA_CPP _gentest_extra_cpp)
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

    _gentest_resolve_codegen_backend(
        TARGET ${target}
        OUT_CODEGEN_TARGET _gentest_codegen_target
        OUT_CODEGEN_EXECUTABLE _gentest_codegen_executable)

    set(_gentest_mock_registry "${_gentest_output_dir}/${_gentest_target_id}_mock_registry.hpp")
    # Generate inline mock implementations as a header; it will be included by
    # the generated wrapper translation units after including sources.
    set(_gentest_mock_impl "${_gentest_output_dir}/${_gentest_target_id}_mock_impl.hpp")
    set(_gentest_mock_registry_domain_outputs "")
    set(_gentest_mock_impl_domain_outputs "")
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
    set(_gentest_depfile "${_gentest_output_dir}/${_gentest_target_id}.gentest.d")

    set(_gentest_codegen_scan_inputs "")
    set(_gentest_replaced_tus "")
    set(_gentest_replaced_source_entries "")
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
            list(APPEND _gentest_replaced_tus ${_gentest_src_abs})
            list(APPEND _gentest_replaced_source_entries ${_gentest_src_entry})
        else()
            list(APPEND _gentest_codegen_scan_inputs ${_gentest_src_abs})
        endif()
    endforeach()

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
        --depfile ${_gentest_depfile}
        --compdb ${CMAKE_BINARY_DIR}
        --source-root ${CMAKE_SOURCE_DIR})

    if(_gentest_mode STREQUAL "manifest")
        list(APPEND _command --output ${_gentest_manifest_output})
        list(APPEND _command --entry ${GENTEST_ENTRY})
        if(GENTEST_NO_INCLUDE_SOURCES)
            list(APPEND _command --no-include-sources)
        endif()
    else()
        list(APPEND _command --tu-out-dir ${_gentest_output_dir})
        foreach(_gentest_wrap_header IN LISTS _gentest_wrapper_headers)
            list(APPEND _command --tu-header-output ${_gentest_wrap_header})
        endforeach()
    endif()

    if(GENTEST_STRICT_FIXTURE)
        list(APPEND _command --strict-fixture)
    endif()
    if(GENTEST_QUIET_CLANG)
        list(APPEND _command --quiet-clang)
    endif()

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
    if(EXISTS "${CMAKE_BINARY_DIR}/compile_commands.json")
        list(APPEND _gentest_codegen_deps "${CMAKE_BINARY_DIR}/compile_commands.json")
    endif()

    cmake_policy(PUSH)
    if(POLICY CMP0171)
        cmake_policy(SET CMP0171 NEW)
    endif()

    if(_gentest_mode STREQUAL "manifest")
        set(_gentest_codegen_outputs
            ${_gentest_manifest_output}
            ${_gentest_mock_registry}
            ${_gentest_mock_impl}
            ${_gentest_mock_registry_domain_outputs}
            ${_gentest_mock_impl_domain_outputs})
    else()
        set(_gentest_codegen_outputs
            ${_gentest_wrapper_headers}
            ${_gentest_module_wrapper_outputs}
            ${_gentest_mock_registry}
            ${_gentest_mock_impl}
            ${_gentest_mock_registry_domain_outputs}
            ${_gentest_mock_impl_domain_outputs})
    endif()

    set(_gentest_custom_command_args
        OUTPUT ${_gentest_codegen_outputs}
        COMMAND ${_command}
        COMMAND_EXPAND_LISTS
        DEPENDS ${_gentest_codegen_deps} ${_gentest_tus} ${GENTEST_DEPENDS}
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

    cmake_policy(POP)

    if(_gentest_mode STREQUAL "manifest")
        _gentest_attach_manifest_codegen(TARGET ${target} OUTPUT ${_gentest_manifest_output})
    else()
        _gentest_attach_tu_wrapper_sources(
            TARGET ${target}
            TARGET_ID ${_gentest_target_id}
            REPLACED_TUS ${_gentest_tus}
            REPLACED_SOURCE_ENTRIES ${_gentest_tu_source_entries}
            WRAPPER_CPP ${_gentest_wrapper_cpp}
            MODULE_NAMES ${_gentest_module_names}
            EXTRA_CPP ${_gentest_extra_cpp}
            CODEGEN_OUTPUTS ${_gentest_codegen_outputs})
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
