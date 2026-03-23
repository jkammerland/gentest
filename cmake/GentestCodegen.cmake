include_guard(GLOBAL)

set(_GENTEST_CODEGEN_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")

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

if(NOT DEFINED GENTEST_CODEGEN_SCAN_DEPS_MODE)
    set(GENTEST_CODEGEN_SCAN_DEPS_MODE "" CACHE STRING
        "Optional gentest_codegen named-module dependency discovery mode override (AUTO, ON, or OFF). Empty keeps the tool default.")
    set_property(CACHE GENTEST_CODEGEN_SCAN_DEPS_MODE PROPERTY STRINGS "" AUTO ON OFF auto on off)
endif()

if(NOT DEFINED GENTEST_CODEGEN_CLANG_SCAN_DEPS)
    set(GENTEST_CODEGEN_CLANG_SCAN_DEPS "" CACHE STRING
        "Optional path to the clang-scan-deps executable used by gentest_codegen for named-module dependency discovery.")
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

function(_gentest_stage_explicit_mock_file stage_dir source_file staged_rel out_staged_files)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source_file}")
    string(MD5 _gentest_stage_key "${stage_dir}|${source_file}")
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

    string(REGEX MATCHALL "#[ \t]*include[ \t]*\"[^\"]+\"" _gentest_include_matches "${_gentest_rewritten_content}")
    foreach(_gentest_include_match IN LISTS _gentest_include_matches)
        if(NOT _gentest_include_match MATCHES "#[ \t]*include[ \t]*\"([^\"]+)\"")
            continue()
        endif()
        set(_gentest_include_path "${CMAKE_MATCH_1}")
        if(IS_ABSOLUTE "${_gentest_include_path}")
            continue()
        endif()

        set(_gentest_include_abs "")
        file(REAL_PATH "${_gentest_include_path}" _gentest_include_candidate BASE_DIRECTORY "${_gentest_source_dir}")
        if(EXISTS "${_gentest_include_candidate}" AND NOT IS_DIRECTORY "${_gentest_include_candidate}")
            set(_gentest_include_abs "${_gentest_include_candidate}")
        else()
            foreach(_gentest_search_root IN LISTS _gentest_search_roots)
                if("${_gentest_search_root}" STREQUAL "" OR "${_gentest_search_root}" MATCHES "\\$<")
                    continue()
                endif()
                file(REAL_PATH "${_gentest_include_path}" _gentest_include_candidate BASE_DIRECTORY "${_gentest_search_root}")
                if(EXISTS "${_gentest_include_candidate}" AND NOT IS_DIRECTORY "${_gentest_include_candidate}")
                    set(_gentest_include_abs "${_gentest_include_candidate}")
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
        set(_gentest_include_replacement "${_gentest_include_match}")
        string(REPLACE "\"${_gentest_include_path}\"" "\"${_gentest_dep_include_rel}\""
            _gentest_include_replacement "${_gentest_include_replacement}")
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

function(_gentest_resolve_codegen_backend)
    set(one_value_args TARGET OUT_CODEGEN_TARGET OUT_CODEGEN_EXECUTABLE)
    cmake_parse_arguments(GENTEST "" "${one_value_args}" "" ${ARGN})

    set(_gentest_installed_codegen "")
    if(NOT CMAKE_CROSSCOMPILING)
        set(_gentest_codegen_prefixes "")
        if(DEFINED PACKAGE_PREFIX_DIR AND NOT PACKAGE_PREFIX_DIR STREQUAL "")
            list(APPEND _gentest_codegen_prefixes "${PACKAGE_PREFIX_DIR}")
        endif()
        get_filename_component(_gentest_codegen_prefix "${_GENTEST_CODEGEN_CMAKE_DIR}/../../.." ABSOLUTE)
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
                set(_gentest_candidate "${_gentest_prefix}/${_gentest_bindir}/gentest_codegen${CMAKE_EXECUTABLE_SUFFIX}")
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
    elseif(NOT _gentest_installed_codegen STREQUAL "")
        set(_gentest_codegen_executable "${_gentest_installed_codegen}")
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

function(_gentest_strip_scan_comments input_text out_text)
    string(LENGTH "${input_text}" _gentest_text_len)
    set(_gentest_result "")
    set(_gentest_idx 0)
    set(_gentest_in_block FALSE)
    set(_gentest_in_line FALSE)

    while(_gentest_idx LESS _gentest_text_len)
        math(EXPR _gentest_remaining "${_gentest_text_len} - ${_gentest_idx}")
        string(SUBSTRING "${input_text}" ${_gentest_idx} 1 _gentest_ch)
        set(_gentest_next "")
        if(_gentest_remaining GREATER 1)
            math(EXPR _gentest_next_idx "${_gentest_idx} + 1")
            string(SUBSTRING "${input_text}" ${_gentest_next_idx} 1 _gentest_next)
        endif()

        if(_gentest_in_block)
            if(_gentest_ch STREQUAL "*" AND _gentest_next STREQUAL "/")
                set(_gentest_in_block FALSE)
                if(NOT _gentest_result STREQUAL "" AND NOT _gentest_result MATCHES "[ \t\r\n]$")
                    string(APPEND _gentest_result " ")
                endif()
                math(EXPR _gentest_idx "${_gentest_idx} + 2")
                continue()
            endif()
            if(_gentest_ch STREQUAL "\n")
                string(APPEND _gentest_result "\n")
            endif()
            math(EXPR _gentest_idx "${_gentest_idx} + 1")
            continue()
        endif()

        if(_gentest_in_line)
            if(_gentest_ch STREQUAL "\n")
                set(_gentest_in_line FALSE)
                string(APPEND _gentest_result "\n")
            endif()
            math(EXPR _gentest_idx "${_gentest_idx} + 1")
            continue()
        endif()

        if(_gentest_ch STREQUAL "/" AND _gentest_next STREQUAL "*")
            set(_gentest_in_block TRUE)
            if(NOT _gentest_result STREQUAL "" AND NOT _gentest_result MATCHES "[ \t\r\n]$")
                string(APPEND _gentest_result " ")
            endif()
            math(EXPR _gentest_idx "${_gentest_idx} + 2")
            continue()
        endif()

        if(_gentest_ch STREQUAL "/" AND _gentest_next STREQUAL "/")
            set(_gentest_in_line TRUE)
            math(EXPR _gentest_idx "${_gentest_idx} + 2")
            continue()
        endif()

        string(APPEND _gentest_result "${_gentest_ch}")
        math(EXPR _gentest_idx "${_gentest_idx} + 1")
    endwhile()

    set(${out_text} "${_gentest_result}" PARENT_SCOPE)
endfunction()

function(_gentest_pp_tokenize_expression expr out_tokens out_known)
    set(_gentest_tokens "")
    string(LENGTH "${expr}" _gentest_expr_len)
    set(_gentest_idx 0)

    while(_gentest_idx LESS _gentest_expr_len)
        string(SUBSTRING "${expr}" ${_gentest_idx} 1 _gentest_ch)
        if(_gentest_ch MATCHES "[ \t\r\n]")
            math(EXPR _gentest_idx "${_gentest_idx} + 1")
            continue()
        endif()

        if(_gentest_ch MATCHES "[A-Za-z_]")
            set(_gentest_token "")
            while(_gentest_idx LESS _gentest_expr_len)
                string(SUBSTRING "${expr}" ${_gentest_idx} 1 _gentest_ident_ch)
                if(NOT _gentest_ident_ch MATCHES "[A-Za-z0-9_]")
                    break()
                endif()
                string(APPEND _gentest_token "${_gentest_ident_ch}")
                math(EXPR _gentest_idx "${_gentest_idx} + 1")
            endwhile()
            list(APPEND _gentest_tokens "${_gentest_token}")
            continue()
        endif()

        if(_gentest_ch MATCHES "[0-9]")
            set(_gentest_token "")
            while(_gentest_idx LESS _gentest_expr_len)
                string(SUBSTRING "${expr}" ${_gentest_idx} 1 _gentest_num_ch)
                if(NOT _gentest_num_ch MATCHES "[A-Za-z0-9_']")
                    break()
                endif()
                string(APPEND _gentest_token "${_gentest_num_ch}")
                math(EXPR _gentest_idx "${_gentest_idx} + 1")
            endwhile()
            list(APPEND _gentest_tokens "${_gentest_token}")
            continue()
        endif()

        set(_gentest_two "")
        math(EXPR _gentest_remaining "${_gentest_expr_len} - ${_gentest_idx}")
        if(_gentest_remaining GREATER 1)
            string(SUBSTRING "${expr}" ${_gentest_idx} 2 _gentest_two)
        endif()
        if(_gentest_two STREQUAL "&&" OR _gentest_two STREQUAL "||" OR _gentest_two STREQUAL "==" OR
           _gentest_two STREQUAL "!=" OR _gentest_two STREQUAL "<=" OR _gentest_two STREQUAL ">=" OR
           _gentest_two STREQUAL "<<" OR _gentest_two STREQUAL ">>")
            list(APPEND _gentest_tokens "${_gentest_two}")
            math(EXPR _gentest_idx "${_gentest_idx} + 2")
            continue()
        endif()

        set(_gentest_single_char_tokens "(" ")" "!" "<" ">" "+" "-" "*" "/" "%" "~" "&" "|" "^")
        list(FIND _gentest_single_char_tokens "${_gentest_ch}" _gentest_single_char_idx)
        if(NOT _gentest_single_char_idx EQUAL -1)
            list(APPEND _gentest_tokens "${_gentest_ch}")
            math(EXPR _gentest_idx "${_gentest_idx} + 1")
            continue()
        endif()

        set(${out_tokens} "" PARENT_SCOPE)
        set(${out_known} FALSE PARENT_SCOPE)
        return()
    endwhile()

    set(${out_tokens} "${_gentest_tokens}" PARENT_SCOPE)
    set(${out_known} TRUE PARENT_SCOPE)
endfunction()

function(_gentest_pp_try_parse_integer_literal token out_known out_value)
    string(TOLOWER "${token}" _gentest_token_lower)
    string(REPLACE "'" "" _gentest_token_lower "${_gentest_token_lower}")
    if(_gentest_token_lower MATCHES "^(0x[0-9a-f]+|0b[01]+|0[0-7]+|[0-9]+)(llu|ull|ll|ul|lu|u|l)?$")
        string(REGEX REPLACE "(llu|ull|ll|ul|lu|u|l)$" "" _gentest_literal_core "${_gentest_token_lower}")
        if(_gentest_literal_core MATCHES "^0b[01]+$")
            string(SUBSTRING "${_gentest_literal_core}" 2 -1 _gentest_binary_digits)
            string(LENGTH "${_gentest_binary_digits}" _gentest_binary_len)
            set(_gentest_idx 0)
            set(_gentest_literal_value 0)
            while(_gentest_idx LESS _gentest_binary_len)
                string(SUBSTRING "${_gentest_binary_digits}" ${_gentest_idx} 1 _gentest_binary_digit)
                math(EXPR _gentest_literal_value "${_gentest_literal_value} * 2 + ${_gentest_binary_digit}")
                math(EXPR _gentest_idx "${_gentest_idx} + 1")
            endwhile()
        elseif(_gentest_literal_core MATCHES "^0[0-7]+$")
            string(LENGTH "${_gentest_literal_core}" _gentest_octal_len)
            set(_gentest_idx 0)
            set(_gentest_literal_value 0)
            while(_gentest_idx LESS _gentest_octal_len)
                string(SUBSTRING "${_gentest_literal_core}" ${_gentest_idx} 1 _gentest_octal_digit)
                math(EXPR _gentest_literal_value "${_gentest_literal_value} * 8 + ${_gentest_octal_digit}")
                math(EXPR _gentest_idx "${_gentest_idx} + 1")
            endwhile()
        else()
            math(EXPR _gentest_literal_value "${_gentest_literal_core}")
        endif()
        set(${out_known} TRUE PARENT_SCOPE)
        set(${out_value} "${_gentest_literal_value}" PARENT_SCOPE)
    else()
        set(${out_known} FALSE PARENT_SCOPE)
        set(${out_value} 0 PARENT_SCOPE)
    endif()
endfunction()

function(_gentest_pp_collect_include_dirs source_dir out_dirs)
    set(_gentest_dirs "")
    if(NOT "${source_dir}" STREQUAL "")
        list(APPEND _gentest_dirs "${source_dir}")
    endif()

    foreach(_gentest_dir IN LISTS ARGN)
        if("${_gentest_dir}" MATCHES "\\$<")
            continue()
        endif()
        get_filename_component(_gentest_abs_dir "${_gentest_dir}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        if(IS_DIRECTORY "${_gentest_abs_dir}")
            list(APPEND _gentest_dirs "${_gentest_abs_dir}")
        endif()
    endforeach()

    foreach(_gentest_var IN ITEMS CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES CMAKE_C_IMPLICIT_INCLUDE_DIRECTORIES)
        foreach(_gentest_dir IN LISTS ${_gentest_var})
            if(IS_DIRECTORY "${_gentest_dir}")
                list(APPEND _gentest_dirs "${_gentest_dir}")
            endif()
        endforeach()
    endforeach()

    if(CMAKE_HOST_WIN32)
        set(_gentest_path_sep ";")
    else()
        set(_gentest_path_sep ":")
    endif()
    foreach(_gentest_env IN ITEMS CPATH CPLUS_INCLUDE_PATH C_INCLUDE_PATH)
        if(DEFINED ENV{${_gentest_env}} AND NOT "$ENV{${_gentest_env}}" STREQUAL "")
            string(REPLACE "${_gentest_path_sep}" ";" _gentest_env_dirs "$ENV{${_gentest_env}}")
            foreach(_gentest_dir IN LISTS _gentest_env_dirs)
                if(IS_DIRECTORY "${_gentest_dir}")
                    list(APPEND _gentest_dirs "${_gentest_dir}")
                endif()
            endforeach()
        endif()
    endforeach()

    foreach(_gentest_dir IN ITEMS
            "/usr/local/include"
            "/usr/include"
            "/opt/homebrew/include"
            "/opt/homebrew/opt/llvm/include/c++/v1"
            "/usr/include/c++/v1"
            "/Library/Developer/CommandLineTools/usr/include/c++/v1")
        if(IS_DIRECTORY "${_gentest_dir}")
            list(APPEND _gentest_dirs "${_gentest_dir}")
        endif()
    endforeach()

    file(GLOB _gentest_globbed_dirs LIST_DIRECTORIES TRUE
        "/usr/include/c++/*"
        "/usr/include/*-linux-gnu/c++/*"
        "/usr/lib/clang/*/include")
    foreach(_gentest_dir IN LISTS _gentest_globbed_dirs)
        if(IS_DIRECTORY "${_gentest_dir}")
            list(APPEND _gentest_dirs "${_gentest_dir}")
        endif()
    endforeach()

    if(DEFINED ENV{SDKROOT} AND NOT "$ENV{SDKROOT}" STREQUAL "")
        foreach(_gentest_dir IN ITEMS
                "$ENV{SDKROOT}/usr/include"
                "$ENV{SDKROOT}/usr/include/c++/v1")
            if(IS_DIRECTORY "${_gentest_dir}")
                list(APPEND _gentest_dirs "${_gentest_dir}")
            endif()
        endforeach()
    endif()

    list(REMOVE_DUPLICATES _gentest_dirs)
    set(${out_dirs} "${_gentest_dirs}" PARENT_SCOPE)
endfunction()

function(_gentest_pp_resolve_has_include_spec include_spec out_known out_spec)
    string(STRIP "${include_spec}" _gentest_spec)
    if(_gentest_spec MATCHES "^\"[^\"]+\"$" OR _gentest_spec MATCHES "^<[^>]+>$")
        set(${out_known} TRUE PARENT_SCOPE)
        set(${out_spec} "${_gentest_spec}" PARENT_SCOPE)
        return()
    endif()

    if(_gentest_spec MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
        set(_gentest_macro_var "_gentest_pp_macro_${_gentest_spec}")
        if(DEFINED ${_gentest_macro_var})
            _gentest_pp_resolve_has_include_spec("${${_gentest_macro_var}}" _gentest_nested_known _gentest_nested_spec)
            set(${out_known} "${_gentest_nested_known}" PARENT_SCOPE)
            set(${out_spec} "${_gentest_nested_spec}" PARENT_SCOPE)
            return()
        endif()
    endif()

    set(${out_known} FALSE PARENT_SCOPE)
    set(${out_spec} "" PARENT_SCOPE)
endfunction()

function(_gentest_pp_eval_has_include include_spec source_dir out_found)
    _gentest_pp_resolve_has_include_spec("${include_spec}" _gentest_spec_known _gentest_spec)
    if(NOT _gentest_spec_known)
        set(${out_found} FALSE PARENT_SCOPE)
        return()
    endif()

    set(_gentest_use_source_dir FALSE)
    if(_gentest_spec MATCHES "^\"([^\"]+)\"$")
        set(_gentest_header "${CMAKE_MATCH_1}")
        set(_gentest_use_source_dir TRUE)
    elseif(_gentest_spec MATCHES "^<([^>]+)>$")
        set(_gentest_header "${CMAKE_MATCH_1}")
    else()
        set(${out_found} FALSE PARENT_SCOPE)
        return()
    endif()

    if(_gentest_use_source_dir AND NOT "${source_dir}" STREQUAL "" AND EXISTS "${source_dir}/${_gentest_header}")
        set(${out_found} TRUE PARENT_SCOPE)
        return()
    endif()

    _gentest_pp_collect_include_dirs("${source_dir}" _gentest_include_dirs ${ARGN})
    foreach(_gentest_dir IN LISTS _gentest_include_dirs)
        if(EXISTS "${_gentest_dir}/${_gentest_header}")
            set(${out_found} TRUE PARENT_SCOPE)
            return()
        endif()
    endforeach()

    set(${out_found} FALSE PARENT_SCOPE)
endfunction()

function(_gentest_pp_rewrite_has_include expr source_dir out_expr)
    set(_gentest_in "${expr}")
    set(_gentest_out "")

    while(1)
        string(REGEX MATCH "__has_include[ \t]*\\(([^)]*)\\)" _gentest_match "${_gentest_in}")
        if(_gentest_match STREQUAL "")
            string(APPEND _gentest_out "${_gentest_in}")
            break()
        endif()

        string(FIND "${_gentest_in}" "${_gentest_match}" _gentest_match_pos)
        string(SUBSTRING "${_gentest_in}" 0 ${_gentest_match_pos} _gentest_prefix)
        string(APPEND _gentest_out "${_gentest_prefix}")

        string(REGEX REPLACE "^__has_include[ \t]*\\(([^)]*)\\)$" "\\1" _gentest_include_spec "${_gentest_match}")
        string(STRIP "${_gentest_include_spec}" _gentest_include_spec)
        _gentest_pp_eval_has_include("${_gentest_include_spec}" "${source_dir}" _gentest_has_include ${ARGN})
        if(_gentest_has_include)
            string(APPEND _gentest_out "1")
        else()
            string(APPEND _gentest_out "0")
        endif()

        string(LENGTH "${_gentest_match}" _gentest_match_len)
        math(EXPR _gentest_suffix_start "${_gentest_match_pos} + ${_gentest_match_len}")
        string(SUBSTRING "${_gentest_in}" ${_gentest_suffix_start} -1 _gentest_in)
    endwhile()

    set(${out_expr} "${_gentest_out}" PARENT_SCOPE)
endfunction()

function(_gentest_canonicalize_module_name raw_name allow_partition_only out_name)
    string(STRIP "${raw_name}" _gentest_name)
    if(_gentest_name STREQUAL "")
        set(${out_name} "" PARENT_SCOPE)
        return()
    endif()

    if(_gentest_name MATCHES "^([A-Za-z_][A-Za-z0-9_]*(\\.[A-Za-z_][A-Za-z0-9_]*)*)([ \t]*:[ \t]*([A-Za-z_][A-Za-z0-9_]*(\\.[A-Za-z_][A-Za-z0-9_]*)*))?$")
        set(_gentest_primary "${CMAKE_MATCH_1}")
        set(_gentest_partition "${CMAKE_MATCH_4}")
        if("${_gentest_partition}" STREQUAL "")
            set(${out_name} "${_gentest_primary}" PARENT_SCOPE)
        else()
            set(${out_name} "${_gentest_primary}:${_gentest_partition}" PARENT_SCOPE)
        endif()
        return()
    endif()

    if(${allow_partition_only} AND _gentest_name MATCHES "^:[ \t]*([A-Za-z_][A-Za-z0-9_]*(\\.[A-Za-z_][A-Za-z0-9_]*)*)$")
        set(${out_name} ":${CMAKE_MATCH_1}" PARENT_SCOPE)
        return()
    endif()

    set(${out_name} "" PARENT_SCOPE)
endfunction()

function(_gentest_pp_token_in_set token out_var)
    set(_gentest_expected_tokens ${ARGN})
    list(FIND _gentest_expected_tokens "${token}" _gentest_token_idx)
    if(_gentest_token_idx EQUAL -1)
        set(${out_var} FALSE PARENT_SCOPE)
    else()
        set(${out_var} TRUE PARENT_SCOPE)
    endif()
endfunction()

function(_gentest_pp_math_binary lhs op rhs out_known out_value)
    _gentest_pp_token_in_set("${op}" _gentest_division_like "/" "%")
    if(_gentest_division_like AND "${rhs}" STREQUAL "0")
        set(${out_known} FALSE PARENT_SCOPE)
        set(${out_value} 0 PARENT_SCOPE)
        return()
    endif()

    math(EXPR _gentest_math_value "(${lhs}) ${op} (${rhs})")
    set(${out_known} TRUE PARENT_SCOPE)
    set(${out_value} "${_gentest_math_value}" PARENT_SCOPE)
endfunction()

function(_gentest_pp_compare_values lhs op rhs out_value)
    set(_gentest_cmp FALSE)
    string(COMPARE EQUAL "${op}" "==" _gentest_op_is_eq)
    string(COMPARE EQUAL "${op}" "!=" _gentest_op_is_ne)
    string(COMPARE EQUAL "${op}" "<" _gentest_op_is_lt)
    string(COMPARE EQUAL "${op}" "<=" _gentest_op_is_le)
    string(COMPARE EQUAL "${op}" ">" _gentest_op_is_gt)
    string(COMPARE EQUAL "${op}" ">=" _gentest_op_is_ge)
    if(_gentest_op_is_eq)
        if("${lhs}" STREQUAL "${rhs}")
            set(_gentest_cmp TRUE)
        endif()
    elseif(_gentest_op_is_ne)
        if(NOT "${lhs}" STREQUAL "${rhs}")
            set(_gentest_cmp TRUE)
        endif()
    elseif(_gentest_op_is_lt)
        if(${lhs} LESS ${rhs})
            set(_gentest_cmp TRUE)
        endif()
    elseif(_gentest_op_is_le)
        if(${lhs} LESS_EQUAL ${rhs})
            set(_gentest_cmp TRUE)
        endif()
    elseif(_gentest_op_is_gt)
        if(${lhs} GREATER ${rhs})
            set(_gentest_cmp TRUE)
        endif()
    elseif(_gentest_op_is_ge)
        if(${lhs} GREATER_EQUAL ${rhs})
            set(_gentest_cmp TRUE)
        endif()
    endif()

    if(_gentest_cmp)
        set(${out_value} 1 PARENT_SCOPE)
    else()
        set(${out_value} 0 PARENT_SCOPE)
    endif()
endfunction()

function(_gentest_pp_parse_primary tokens_var idx depth out_known out_value out_next_idx)
    list(LENGTH ${tokens_var} _gentest_token_count)
    if(${idx} GREATER_EQUAL ${_gentest_token_count})
        set(${out_known} FALSE PARENT_SCOPE)
        set(${out_value} 0 PARENT_SCOPE)
        set(${out_next_idx} "${idx}" PARENT_SCOPE)
        return()
    endif()

    list(GET ${tokens_var} ${idx} _gentest_token)
    string(COMPARE EQUAL "${_gentest_token}" "(" _gentest_token_is_lparen)
    if(_gentest_token_is_lparen)
        math(EXPR _gentest_subexpr_idx "${idx} + 1")
        _gentest_pp_parse_logical_or(${tokens_var} ${_gentest_subexpr_idx} ${depth}
            _gentest_known _gentest_value _gentest_next_idx)
        if(NOT _gentest_known)
            set(${out_known} FALSE PARENT_SCOPE)
            set(${out_value} 0 PARENT_SCOPE)
            set(${out_next_idx} "${idx}" PARENT_SCOPE)
            return()
        endif()
        if(_gentest_next_idx GREATER_EQUAL _gentest_token_count)
            set(${out_known} FALSE PARENT_SCOPE)
            set(${out_value} 0 PARENT_SCOPE)
            set(${out_next_idx} "${idx}" PARENT_SCOPE)
            return()
        endif()
        list(GET ${tokens_var} ${_gentest_next_idx} _gentest_close)
        string(COMPARE EQUAL "${_gentest_close}" ")" _gentest_close_is_rparen)
        if(NOT _gentest_close_is_rparen)
            set(${out_known} FALSE PARENT_SCOPE)
            set(${out_value} 0 PARENT_SCOPE)
            set(${out_next_idx} "${idx}" PARENT_SCOPE)
            return()
        endif()
        math(EXPR _gentest_next_idx "${_gentest_next_idx} + 1")
        set(${out_known} TRUE PARENT_SCOPE)
        set(${out_value} "${_gentest_value}" PARENT_SCOPE)
        set(${out_next_idx} "${_gentest_next_idx}" PARENT_SCOPE)
        return()
    endif()

    if("${_gentest_token}" STREQUAL "defined")
        math(EXPR _gentest_next_idx "${idx} + 1")
        if(_gentest_next_idx GREATER_EQUAL _gentest_token_count)
            set(${out_known} FALSE PARENT_SCOPE)
            set(${out_value} 0 PARENT_SCOPE)
            set(${out_next_idx} "${idx}" PARENT_SCOPE)
            return()
        endif()

        list(GET ${tokens_var} ${_gentest_next_idx} _gentest_next)
        string(COMPARE EQUAL "${_gentest_next}" "(" _gentest_next_is_lparen)
        if(_gentest_next_is_lparen)
            math(EXPR _gentest_ident_idx "${_gentest_next_idx} + 1")
            if(_gentest_ident_idx GREATER_EQUAL _gentest_token_count)
                set(${out_known} FALSE PARENT_SCOPE)
                set(${out_value} 0 PARENT_SCOPE)
                set(${out_next_idx} "${idx}" PARENT_SCOPE)
                return()
            endif()
            list(GET ${tokens_var} ${_gentest_ident_idx} _gentest_ident)
            math(EXPR _gentest_close_idx "${_gentest_ident_idx} + 1")
            if(_gentest_close_idx GREATER_EQUAL _gentest_token_count)
                set(${out_known} FALSE PARENT_SCOPE)
                set(${out_value} 0 PARENT_SCOPE)
                set(${out_next_idx} "${idx}" PARENT_SCOPE)
                return()
            endif()
            list(GET ${tokens_var} ${_gentest_close_idx} _gentest_close)
            string(COMPARE EQUAL "${_gentest_close}" ")" _gentest_close_is_rparen)
            if(NOT _gentest_close_is_rparen)
                set(${out_known} FALSE PARENT_SCOPE)
                set(${out_value} 0 PARENT_SCOPE)
                set(${out_next_idx} "${idx}" PARENT_SCOPE)
                return()
            endif()
            math(EXPR _gentest_result_next "${_gentest_close_idx} + 1")
        else()
            set(_gentest_ident "${_gentest_next}")
            math(EXPR _gentest_result_next "${_gentest_next_idx} + 1")
        endif()

        if(NOT _gentest_ident MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
            set(${out_known} FALSE PARENT_SCOPE)
            set(${out_value} 0 PARENT_SCOPE)
            set(${out_next_idx} "${idx}" PARENT_SCOPE)
            return()
        endif()

        set(_gentest_macro_var "_gentest_pp_macro_${_gentest_ident}")
        if(DEFINED ${_gentest_macro_var})
            set(_gentest_defined_value 1)
        else()
            set(_gentest_defined_value 0)
        endif()
        set(${out_known} TRUE PARENT_SCOPE)
        set(${out_value} "${_gentest_defined_value}" PARENT_SCOPE)
        set(${out_next_idx} "${_gentest_result_next}" PARENT_SCOPE)
        return()
    endif()

    _gentest_pp_try_parse_integer_literal("${_gentest_token}" _gentest_literal_known _gentest_literal_value)
    if(_gentest_literal_known)
        math(EXPR _gentest_next_idx "${idx} + 1")
        set(${out_known} TRUE PARENT_SCOPE)
        set(${out_value} "${_gentest_literal_value}" PARENT_SCOPE)
        set(${out_next_idx} "${_gentest_next_idx}" PARENT_SCOPE)
        return()
    endif()

    if("${_gentest_token}" MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
        set(_gentest_macro_var "_gentest_pp_macro_${_gentest_token}")
        if(DEFINED ${_gentest_macro_var})
            string(STRIP "${${_gentest_macro_var}}" _gentest_macro_value)
            if(_gentest_macro_value STREQUAL "")
                set(_gentest_expanded_value 1)
                set(_gentest_expanded_known TRUE)
            else()
                _gentest_pp_try_parse_integer_literal("${_gentest_macro_value}" _gentest_expanded_known _gentest_expanded_value)
                if(NOT _gentest_expanded_known)
                    if(${depth} GREATER_EQUAL 32)
                        set(${out_known} FALSE PARENT_SCOPE)
                        set(${out_value} 0 PARENT_SCOPE)
                        set(${out_next_idx} "${idx}" PARENT_SCOPE)
                        return()
                    endif()
                    math(EXPR _gentest_next_depth "${depth} + 1")
                    _gentest_pp_eval_numeric_expression("${_gentest_macro_value}" ${_gentest_next_depth}
                        _gentest_expanded_known _gentest_expanded_value)
                endif()
            endif()
            if(NOT _gentest_expanded_known)
                set(${out_known} FALSE PARENT_SCOPE)
                set(${out_value} 0 PARENT_SCOPE)
                set(${out_next_idx} "${idx}" PARENT_SCOPE)
                return()
            endif()
        else()
            set(_gentest_expanded_value 0)
        endif()

        math(EXPR _gentest_next_idx "${idx} + 1")
        set(${out_known} TRUE PARENT_SCOPE)
        set(${out_value} "${_gentest_expanded_value}" PARENT_SCOPE)
        set(${out_next_idx} "${_gentest_next_idx}" PARENT_SCOPE)
        return()
    endif()

    set(${out_known} FALSE PARENT_SCOPE)
    set(${out_value} 0 PARENT_SCOPE)
    set(${out_next_idx} "${idx}" PARENT_SCOPE)
endfunction()

function(_gentest_pp_parse_unary tokens_var idx depth out_known out_value out_next_idx)
    list(LENGTH ${tokens_var} _gentest_token_count)
    if(${idx} GREATER_EQUAL ${_gentest_token_count})
        set(${out_known} FALSE PARENT_SCOPE)
        set(${out_value} 0 PARENT_SCOPE)
        set(${out_next_idx} "${idx}" PARENT_SCOPE)
        return()
    endif()

    list(GET ${tokens_var} ${idx} _gentest_token)
    _gentest_pp_token_in_set("${_gentest_token}" _gentest_is_unary_op "!" "~" "+" "-")
    if(_gentest_is_unary_op)
        math(EXPR _gentest_operand_idx "${idx} + 1")
        _gentest_pp_parse_unary(${tokens_var} ${_gentest_operand_idx} ${depth}
            _gentest_known _gentest_value _gentest_next_idx)
        if(NOT _gentest_known)
            set(${out_known} FALSE PARENT_SCOPE)
            set(${out_value} 0 PARENT_SCOPE)
            set(${out_next_idx} "${idx}" PARENT_SCOPE)
            return()
        endif()

        string(COMPARE EQUAL "${_gentest_token}" "!" _gentest_token_is_not)
        string(COMPARE EQUAL "${_gentest_token}" "~" _gentest_token_is_bitnot)
        string(COMPARE EQUAL "${_gentest_token}" "+" _gentest_token_is_plus)
        if(_gentest_token_is_not)
            if("${_gentest_value}" STREQUAL "0")
                set(_gentest_result 1)
            else()
                set(_gentest_result 0)
            endif()
        elseif(_gentest_token_is_bitnot)
            math(EXPR _gentest_result "~(${_gentest_value})")
        elseif(_gentest_token_is_plus)
            set(_gentest_result "${_gentest_value}")
        else()
            math(EXPR _gentest_result "-(${_gentest_value})")
        endif()

        set(${out_known} TRUE PARENT_SCOPE)
        set(${out_value} "${_gentest_result}" PARENT_SCOPE)
        set(${out_next_idx} "${_gentest_next_idx}" PARENT_SCOPE)
        return()
    endif()

    _gentest_pp_parse_primary(${tokens_var} ${idx} ${depth}
        _gentest_known _gentest_value _gentest_next_idx)
    set(${out_known} "${_gentest_known}" PARENT_SCOPE)
    set(${out_value} "${_gentest_value}" PARENT_SCOPE)
    set(${out_next_idx} "${_gentest_next_idx}" PARENT_SCOPE)
endfunction()

function(_gentest_pp_parse_multiplicative tokens_var idx depth out_known out_value out_next_idx)
    _gentest_pp_parse_unary(${tokens_var} ${idx} ${depth} _gentest_known _gentest_value _gentest_next_idx)
    if(NOT _gentest_known)
        set(${out_known} FALSE PARENT_SCOPE)
        set(${out_value} 0 PARENT_SCOPE)
        set(${out_next_idx} "${idx}" PARENT_SCOPE)
        return()
    endif()

    list(LENGTH ${tokens_var} _gentest_token_count)
    while(_gentest_next_idx LESS _gentest_token_count)
        list(GET ${tokens_var} ${_gentest_next_idx} _gentest_op)
        _gentest_pp_token_in_set("${_gentest_op}" _gentest_is_mul_op "*" "/" "%")
        if(NOT _gentest_is_mul_op)
            break()
        endif()
        math(EXPR _gentest_rhs_idx "${_gentest_next_idx} + 1")
        _gentest_pp_parse_unary(${tokens_var} ${_gentest_rhs_idx} ${depth}
            _gentest_rhs_known _gentest_rhs_value _gentest_after_rhs)
        if(NOT _gentest_rhs_known)
            set(${out_known} FALSE PARENT_SCOPE)
            set(${out_value} 0 PARENT_SCOPE)
            set(${out_next_idx} "${idx}" PARENT_SCOPE)
            return()
        endif()
        _gentest_pp_math_binary("${_gentest_value}" "${_gentest_op}" "${_gentest_rhs_value}"
            _gentest_math_known _gentest_value)
        if(NOT _gentest_math_known)
            set(${out_known} FALSE PARENT_SCOPE)
            set(${out_value} 0 PARENT_SCOPE)
            set(${out_next_idx} "${idx}" PARENT_SCOPE)
            return()
        endif()
        set(_gentest_next_idx "${_gentest_after_rhs}")
    endwhile()

    set(${out_known} TRUE PARENT_SCOPE)
    set(${out_value} "${_gentest_value}" PARENT_SCOPE)
    set(${out_next_idx} "${_gentest_next_idx}" PARENT_SCOPE)
endfunction()

function(_gentest_pp_parse_additive tokens_var idx depth out_known out_value out_next_idx)
    _gentest_pp_parse_multiplicative(${tokens_var} ${idx} ${depth} _gentest_known _gentest_value _gentest_next_idx)
    if(NOT _gentest_known)
        set(${out_known} FALSE PARENT_SCOPE)
        set(${out_value} 0 PARENT_SCOPE)
        set(${out_next_idx} "${idx}" PARENT_SCOPE)
        return()
    endif()

    list(LENGTH ${tokens_var} _gentest_token_count)
    while(_gentest_next_idx LESS _gentest_token_count)
        list(GET ${tokens_var} ${_gentest_next_idx} _gentest_op)
        _gentest_pp_token_in_set("${_gentest_op}" _gentest_is_add_op "+" "-")
        if(NOT _gentest_is_add_op)
            break()
        endif()
        math(EXPR _gentest_rhs_idx "${_gentest_next_idx} + 1")
        _gentest_pp_parse_multiplicative(${tokens_var} ${_gentest_rhs_idx} ${depth}
            _gentest_rhs_known _gentest_rhs_value _gentest_after_rhs)
        if(NOT _gentest_rhs_known)
            set(${out_known} FALSE PARENT_SCOPE)
            set(${out_value} 0 PARENT_SCOPE)
            set(${out_next_idx} "${idx}" PARENT_SCOPE)
            return()
        endif()
        _gentest_pp_math_binary("${_gentest_value}" "${_gentest_op}" "${_gentest_rhs_value}"
            _gentest_math_known _gentest_value)
        if(NOT _gentest_math_known)
            set(${out_known} FALSE PARENT_SCOPE)
            set(${out_value} 0 PARENT_SCOPE)
            set(${out_next_idx} "${idx}" PARENT_SCOPE)
            return()
        endif()
        set(_gentest_next_idx "${_gentest_after_rhs}")
    endwhile()

    set(${out_known} TRUE PARENT_SCOPE)
    set(${out_value} "${_gentest_value}" PARENT_SCOPE)
    set(${out_next_idx} "${_gentest_next_idx}" PARENT_SCOPE)
endfunction()

function(_gentest_pp_parse_shift tokens_var idx depth out_known out_value out_next_idx)
    _gentest_pp_parse_additive(${tokens_var} ${idx} ${depth} _gentest_known _gentest_value _gentest_next_idx)
    if(NOT _gentest_known)
        set(${out_known} FALSE PARENT_SCOPE)
        set(${out_value} 0 PARENT_SCOPE)
        set(${out_next_idx} "${idx}" PARENT_SCOPE)
        return()
    endif()

    list(LENGTH ${tokens_var} _gentest_token_count)
    while(_gentest_next_idx LESS _gentest_token_count)
        list(GET ${tokens_var} ${_gentest_next_idx} _gentest_op)
        _gentest_pp_token_in_set("${_gentest_op}" _gentest_is_shift_op "<<" ">>")
        if(NOT _gentest_is_shift_op)
            break()
        endif()
        math(EXPR _gentest_rhs_idx "${_gentest_next_idx} + 1")
        _gentest_pp_parse_additive(${tokens_var} ${_gentest_rhs_idx} ${depth}
            _gentest_rhs_known _gentest_rhs_value _gentest_after_rhs)
        if(NOT _gentest_rhs_known)
            set(${out_known} FALSE PARENT_SCOPE)
            set(${out_value} 0 PARENT_SCOPE)
            set(${out_next_idx} "${idx}" PARENT_SCOPE)
            return()
        endif()
        _gentest_pp_math_binary("${_gentest_value}" "${_gentest_op}" "${_gentest_rhs_value}"
            _gentest_math_known _gentest_value)
        if(NOT _gentest_math_known)
            set(${out_known} FALSE PARENT_SCOPE)
            set(${out_value} 0 PARENT_SCOPE)
            set(${out_next_idx} "${idx}" PARENT_SCOPE)
            return()
        endif()
        set(_gentest_next_idx "${_gentest_after_rhs}")
    endwhile()

    set(${out_known} TRUE PARENT_SCOPE)
    set(${out_value} "${_gentest_value}" PARENT_SCOPE)
    set(${out_next_idx} "${_gentest_next_idx}" PARENT_SCOPE)
endfunction()

function(_gentest_pp_parse_relational tokens_var idx depth out_known out_value out_next_idx)
    _gentest_pp_parse_shift(${tokens_var} ${idx} ${depth} _gentest_known _gentest_value _gentest_next_idx)
    if(NOT _gentest_known)
        set(${out_known} FALSE PARENT_SCOPE)
        set(${out_value} 0 PARENT_SCOPE)
        set(${out_next_idx} "${idx}" PARENT_SCOPE)
        return()
    endif()

    list(LENGTH ${tokens_var} _gentest_token_count)
    while(_gentest_next_idx LESS _gentest_token_count)
        list(GET ${tokens_var} ${_gentest_next_idx} _gentest_op)
        _gentest_pp_token_in_set("${_gentest_op}" _gentest_is_rel_op "<" "<=" ">" ">=")
        if(NOT _gentest_is_rel_op)
            break()
        endif()
        math(EXPR _gentest_rhs_idx "${_gentest_next_idx} + 1")
        _gentest_pp_parse_shift(${tokens_var} ${_gentest_rhs_idx} ${depth}
            _gentest_rhs_known _gentest_rhs_value _gentest_after_rhs)
        if(NOT _gentest_rhs_known)
            set(${out_known} FALSE PARENT_SCOPE)
            set(${out_value} 0 PARENT_SCOPE)
            set(${out_next_idx} "${idx}" PARENT_SCOPE)
            return()
        endif()
        _gentest_pp_compare_values("${_gentest_value}" "${_gentest_op}" "${_gentest_rhs_value}" _gentest_value)
        set(_gentest_next_idx "${_gentest_after_rhs}")
    endwhile()

    set(${out_known} TRUE PARENT_SCOPE)
    set(${out_value} "${_gentest_value}" PARENT_SCOPE)
    set(${out_next_idx} "${_gentest_next_idx}" PARENT_SCOPE)
endfunction()

function(_gentest_pp_parse_equality tokens_var idx depth out_known out_value out_next_idx)
    _gentest_pp_parse_relational(${tokens_var} ${idx} ${depth} _gentest_known _gentest_value _gentest_next_idx)
    if(NOT _gentest_known)
        set(${out_known} FALSE PARENT_SCOPE)
        set(${out_value} 0 PARENT_SCOPE)
        set(${out_next_idx} "${idx}" PARENT_SCOPE)
        return()
    endif()

    list(LENGTH ${tokens_var} _gentest_token_count)
    while(_gentest_next_idx LESS _gentest_token_count)
        list(GET ${tokens_var} ${_gentest_next_idx} _gentest_op)
        _gentest_pp_token_in_set("${_gentest_op}" _gentest_is_eq_op "==" "!=")
        if(NOT _gentest_is_eq_op)
            break()
        endif()
        math(EXPR _gentest_rhs_idx "${_gentest_next_idx} + 1")
        _gentest_pp_parse_relational(${tokens_var} ${_gentest_rhs_idx} ${depth}
            _gentest_rhs_known _gentest_rhs_value _gentest_after_rhs)
        if(NOT _gentest_rhs_known)
            set(${out_known} FALSE PARENT_SCOPE)
            set(${out_value} 0 PARENT_SCOPE)
            set(${out_next_idx} "${idx}" PARENT_SCOPE)
            return()
        endif()
        _gentest_pp_compare_values("${_gentest_value}" "${_gentest_op}" "${_gentest_rhs_value}" _gentest_value)
        set(_gentest_next_idx "${_gentest_after_rhs}")
    endwhile()

    set(${out_known} TRUE PARENT_SCOPE)
    set(${out_value} "${_gentest_value}" PARENT_SCOPE)
    set(${out_next_idx} "${_gentest_next_idx}" PARENT_SCOPE)
endfunction()

function(_gentest_pp_parse_bitwise_and tokens_var idx depth out_known out_value out_next_idx)
    _gentest_pp_parse_equality(${tokens_var} ${idx} ${depth} _gentest_known _gentest_value _gentest_next_idx)
    if(NOT _gentest_known)
        set(${out_known} FALSE PARENT_SCOPE)
        set(${out_value} 0 PARENT_SCOPE)
        set(${out_next_idx} "${idx}" PARENT_SCOPE)
        return()
    endif()

    list(LENGTH ${tokens_var} _gentest_token_count)
    while(_gentest_next_idx LESS _gentest_token_count)
        list(GET ${tokens_var} ${_gentest_next_idx} _gentest_op)
        string(COMPARE EQUAL "${_gentest_op}" "&" _gentest_op_is_bitand)
        if(NOT _gentest_op_is_bitand)
            break()
        endif()
        math(EXPR _gentest_rhs_idx "${_gentest_next_idx} + 1")
        _gentest_pp_parse_equality(${tokens_var} ${_gentest_rhs_idx} ${depth}
            _gentest_rhs_known _gentest_rhs_value _gentest_after_rhs)
        if(NOT _gentest_rhs_known)
            set(${out_known} FALSE PARENT_SCOPE)
            set(${out_value} 0 PARENT_SCOPE)
            set(${out_next_idx} "${idx}" PARENT_SCOPE)
            return()
        endif()
        _gentest_pp_math_binary("${_gentest_value}" "&" "${_gentest_rhs_value}" _gentest_math_known _gentest_value)
        if(NOT _gentest_math_known)
            set(${out_known} FALSE PARENT_SCOPE)
            set(${out_value} 0 PARENT_SCOPE)
            set(${out_next_idx} "${idx}" PARENT_SCOPE)
            return()
        endif()
        set(_gentest_next_idx "${_gentest_after_rhs}")
    endwhile()

    set(${out_known} TRUE PARENT_SCOPE)
    set(${out_value} "${_gentest_value}" PARENT_SCOPE)
    set(${out_next_idx} "${_gentest_next_idx}" PARENT_SCOPE)
endfunction()

function(_gentest_pp_parse_bitwise_xor tokens_var idx depth out_known out_value out_next_idx)
    _gentest_pp_parse_bitwise_and(${tokens_var} ${idx} ${depth} _gentest_known _gentest_value _gentest_next_idx)
    if(NOT _gentest_known)
        set(${out_known} FALSE PARENT_SCOPE)
        set(${out_value} 0 PARENT_SCOPE)
        set(${out_next_idx} "${idx}" PARENT_SCOPE)
        return()
    endif()

    list(LENGTH ${tokens_var} _gentest_token_count)
    while(_gentest_next_idx LESS _gentest_token_count)
        list(GET ${tokens_var} ${_gentest_next_idx} _gentest_op)
        string(COMPARE EQUAL "${_gentest_op}" "^" _gentest_op_is_bitxor)
        if(NOT _gentest_op_is_bitxor)
            break()
        endif()
        math(EXPR _gentest_rhs_idx "${_gentest_next_idx} + 1")
        _gentest_pp_parse_bitwise_and(${tokens_var} ${_gentest_rhs_idx} ${depth}
            _gentest_rhs_known _gentest_rhs_value _gentest_after_rhs)
        if(NOT _gentest_rhs_known)
            set(${out_known} FALSE PARENT_SCOPE)
            set(${out_value} 0 PARENT_SCOPE)
            set(${out_next_idx} "${idx}" PARENT_SCOPE)
            return()
        endif()
        _gentest_pp_math_binary("${_gentest_value}" "^" "${_gentest_rhs_value}" _gentest_math_known _gentest_value)
        if(NOT _gentest_math_known)
            set(${out_known} FALSE PARENT_SCOPE)
            set(${out_value} 0 PARENT_SCOPE)
            set(${out_next_idx} "${idx}" PARENT_SCOPE)
            return()
        endif()
        set(_gentest_next_idx "${_gentest_after_rhs}")
    endwhile()

    set(${out_known} TRUE PARENT_SCOPE)
    set(${out_value} "${_gentest_value}" PARENT_SCOPE)
    set(${out_next_idx} "${_gentest_next_idx}" PARENT_SCOPE)
endfunction()

function(_gentest_pp_parse_bitwise_or tokens_var idx depth out_known out_value out_next_idx)
    _gentest_pp_parse_bitwise_xor(${tokens_var} ${idx} ${depth} _gentest_known _gentest_value _gentest_next_idx)
    if(NOT _gentest_known)
        set(${out_known} FALSE PARENT_SCOPE)
        set(${out_value} 0 PARENT_SCOPE)
        set(${out_next_idx} "${idx}" PARENT_SCOPE)
        return()
    endif()

    list(LENGTH ${tokens_var} _gentest_token_count)
    while(_gentest_next_idx LESS _gentest_token_count)
        list(GET ${tokens_var} ${_gentest_next_idx} _gentest_op)
        string(COMPARE EQUAL "${_gentest_op}" "|" _gentest_op_is_bitor)
        if(NOT _gentest_op_is_bitor)
            break()
        endif()
        math(EXPR _gentest_rhs_idx "${_gentest_next_idx} + 1")
        _gentest_pp_parse_bitwise_xor(${tokens_var} ${_gentest_rhs_idx} ${depth}
            _gentest_rhs_known _gentest_rhs_value _gentest_after_rhs)
        if(NOT _gentest_rhs_known)
            set(${out_known} FALSE PARENT_SCOPE)
            set(${out_value} 0 PARENT_SCOPE)
            set(${out_next_idx} "${idx}" PARENT_SCOPE)
            return()
        endif()
        _gentest_pp_math_binary("${_gentest_value}" "|" "${_gentest_rhs_value}" _gentest_math_known _gentest_value)
        if(NOT _gentest_math_known)
            set(${out_known} FALSE PARENT_SCOPE)
            set(${out_value} 0 PARENT_SCOPE)
            set(${out_next_idx} "${idx}" PARENT_SCOPE)
            return()
        endif()
        set(_gentest_next_idx "${_gentest_after_rhs}")
    endwhile()

    set(${out_known} TRUE PARENT_SCOPE)
    set(${out_value} "${_gentest_value}" PARENT_SCOPE)
    set(${out_next_idx} "${_gentest_next_idx}" PARENT_SCOPE)
endfunction()

function(_gentest_pp_parse_logical_and tokens_var idx depth out_known out_value out_next_idx)
    _gentest_pp_parse_bitwise_or(${tokens_var} ${idx} ${depth} _gentest_known _gentest_value _gentest_next_idx)
    if(NOT _gentest_known)
        set(${out_known} FALSE PARENT_SCOPE)
        set(${out_value} 0 PARENT_SCOPE)
        set(${out_next_idx} "${idx}" PARENT_SCOPE)
        return()
    endif()

    list(LENGTH ${tokens_var} _gentest_token_count)
    while(_gentest_next_idx LESS _gentest_token_count)
        list(GET ${tokens_var} ${_gentest_next_idx} _gentest_op)
        if(NOT "${_gentest_op}" STREQUAL "&&")
            break()
        endif()
        math(EXPR _gentest_rhs_idx "${_gentest_next_idx} + 1")
        _gentest_pp_parse_bitwise_or(${tokens_var} ${_gentest_rhs_idx} ${depth}
            _gentest_rhs_known _gentest_rhs_value _gentest_after_rhs)
        if(NOT _gentest_rhs_known)
            set(${out_known} FALSE PARENT_SCOPE)
            set(${out_value} 0 PARENT_SCOPE)
            set(${out_next_idx} "${idx}" PARENT_SCOPE)
            return()
        endif()
        if(NOT "${_gentest_value}" STREQUAL "0" AND NOT "${_gentest_rhs_value}" STREQUAL "0")
            set(_gentest_value 1)
        else()
            set(_gentest_value 0)
        endif()
        set(_gentest_next_idx "${_gentest_after_rhs}")
    endwhile()

    set(${out_known} TRUE PARENT_SCOPE)
    set(${out_value} "${_gentest_value}" PARENT_SCOPE)
    set(${out_next_idx} "${_gentest_next_idx}" PARENT_SCOPE)
endfunction()

function(_gentest_pp_parse_logical_or tokens_var idx depth out_known out_value out_next_idx)
    _gentest_pp_parse_logical_and(${tokens_var} ${idx} ${depth} _gentest_known _gentest_value _gentest_next_idx)
    if(NOT _gentest_known)
        set(${out_known} FALSE PARENT_SCOPE)
        set(${out_value} 0 PARENT_SCOPE)
        set(${out_next_idx} "${idx}" PARENT_SCOPE)
        return()
    endif()

    list(LENGTH ${tokens_var} _gentest_token_count)
    while(_gentest_next_idx LESS _gentest_token_count)
        list(GET ${tokens_var} ${_gentest_next_idx} _gentest_op)
        if(NOT "${_gentest_op}" STREQUAL "||")
            break()
        endif()
        math(EXPR _gentest_rhs_idx "${_gentest_next_idx} + 1")
        _gentest_pp_parse_logical_and(${tokens_var} ${_gentest_rhs_idx} ${depth}
            _gentest_rhs_known _gentest_rhs_value _gentest_after_rhs)
        if(NOT _gentest_rhs_known)
            set(${out_known} FALSE PARENT_SCOPE)
            set(${out_value} 0 PARENT_SCOPE)
            set(${out_next_idx} "${idx}" PARENT_SCOPE)
            return()
        endif()
        if(NOT "${_gentest_value}" STREQUAL "0" OR NOT "${_gentest_rhs_value}" STREQUAL "0")
            set(_gentest_value 1)
        else()
            set(_gentest_value 0)
        endif()
        set(_gentest_next_idx "${_gentest_after_rhs}")
    endwhile()

    set(${out_known} TRUE PARENT_SCOPE)
    set(${out_value} "${_gentest_value}" PARENT_SCOPE)
    set(${out_next_idx} "${_gentest_next_idx}" PARENT_SCOPE)
endfunction()

function(_gentest_pp_eval_numeric_expression expr depth out_known out_value)
    if(${depth} GREATER 32)
        set(${out_known} FALSE PARENT_SCOPE)
        set(${out_value} 0 PARENT_SCOPE)
        return()
    endif()

    _gentest_pp_tokenize_expression("${expr}" _gentest_tokens _gentest_tokens_known)
    if(NOT _gentest_tokens_known)
        set(${out_known} FALSE PARENT_SCOPE)
        set(${out_value} 0 PARENT_SCOPE)
        return()
    endif()

    list(LENGTH _gentest_tokens _gentest_token_count)
    if(_gentest_token_count EQUAL 0)
        set(${out_known} FALSE PARENT_SCOPE)
        set(${out_value} 0 PARENT_SCOPE)
        return()
    endif()

    _gentest_pp_parse_logical_or(_gentest_tokens 0 ${depth} _gentest_known _gentest_value _gentest_next_idx)
    if(NOT _gentest_known OR NOT _gentest_next_idx EQUAL _gentest_token_count)
        set(${out_known} FALSE PARENT_SCOPE)
        set(${out_value} 0 PARENT_SCOPE)
        return()
    endif()

    set(${out_known} TRUE PARENT_SCOPE)
    set(${out_value} "${_gentest_value}" PARENT_SCOPE)
endfunction()

function(_gentest_pp_eval_condition expr source_dir out_known out_value)
    _gentest_pp_rewrite_has_include("${expr}" "${source_dir}" _gentest_rewritten_expr ${ARGN})
    _gentest_pp_eval_numeric_expression("${_gentest_rewritten_expr}" 0 _gentest_eval_known _gentest_eval_value)
    if(NOT _gentest_eval_known)
        set(${out_known} FALSE PARENT_SCOPE)
        set(${out_value} FALSE PARENT_SCOPE)
        return()
    endif()

    if("${_gentest_eval_value}" STREQUAL "0")
        set(_gentest_eval_bool FALSE)
    else()
        set(_gentest_eval_bool TRUE)
    endif()

    set(${out_known} TRUE PARENT_SCOPE)
    set(${out_value} "${_gentest_eval_bool}" PARENT_SCOPE)
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
    file(READ "${input}" _gentest_module_text)
    _gentest_strip_scan_comments("${_gentest_module_text}" _gentest_module_text)
    string(REPLACE "\r\n" "\n" _gentest_module_text "${_gentest_module_text}")
    string(REPLACE "\r" "\n" _gentest_module_text "${_gentest_module_text}")

    set(_gentest_module_name "")
    set(_gentest_idx 0)
    string(LENGTH "${_gentest_module_text}" _gentest_text_len)
    set(_gentest_current_active TRUE)
    set(_gentest_if_parent_stack "")
    set(_gentest_if_taken_stack "")
    set(_gentest_if_active_stack "")
    set(_gentest_pp_buffer "")
    set(_gentest_pp_continuation FALSE)
    set(_gentest_statement_buffer "")
    get_filename_component(_gentest_source_dir "${input}" DIRECTORY)
    set(_gentest_include_dirs ${ARGN})

    while(_gentest_idx LESS _gentest_text_len AND _gentest_module_name STREQUAL "")
        math(EXPR _gentest_remaining_len "${_gentest_text_len} - ${_gentest_idx}")
        string(SUBSTRING "${_gentest_module_text}" ${_gentest_idx} ${_gentest_remaining_len} _gentest_remaining_text)
        string(FIND "${_gentest_remaining_text}" "\n" _gentest_line_break)
        if(_gentest_line_break EQUAL -1)
            set(_gentest_line_end ${_gentest_text_len})
            set(_gentest_next_idx ${_gentest_text_len})
        else()
            math(EXPR _gentest_line_end "${_gentest_idx} + ${_gentest_line_break}")
            math(EXPR _gentest_next_idx "${_gentest_line_end} + 1")
        endif()
        math(EXPR _gentest_line_len "${_gentest_line_end} - ${_gentest_idx}")
        string(SUBSTRING "${_gentest_module_text}" ${_gentest_idx} ${_gentest_line_len} _gentest_line)
        set(_gentest_idx ${_gentest_next_idx})

        string(REGEX REPLACE "^[ \t\r\n]+" "" _gentest_trimmed "${_gentest_line}")
        string(REGEX REPLACE "[ \t\r\n]+$" "" _gentest_trimmed "${_gentest_trimmed}")

        set(_gentest_is_preprocessor FALSE)
        if(_gentest_pp_continuation)
            set(_gentest_is_preprocessor TRUE)
        elseif(_gentest_trimmed MATCHES "^#")
            set(_gentest_is_preprocessor TRUE)
        endif()

        if(_gentest_is_preprocessor)
            if(NOT _gentest_trimmed STREQUAL "")
                if(NOT _gentest_pp_buffer STREQUAL "")
                    string(APPEND _gentest_pp_buffer " ")
                endif()
                string(APPEND _gentest_pp_buffer "${_gentest_trimmed}")
            endif()

            set(_gentest_pp_continuation FALSE)
            if(_gentest_trimmed MATCHES "\\\\[ \t]*$")
                set(_gentest_pp_continuation TRUE)
                string(REGEX REPLACE "\\\\[ \t]*$" "" _gentest_pp_buffer "${_gentest_pp_buffer}")
                string(REGEX REPLACE "[ \t\r\n]+$" "" _gentest_pp_buffer "${_gentest_pp_buffer}")
            endif()

            if(NOT _gentest_pp_continuation AND NOT _gentest_pp_buffer STREQUAL "")
                if(_gentest_pp_buffer MATCHES "^#[ \t]*([A-Za-z]+)(.*)$")
                    set(_gentest_pp_keyword "${CMAKE_MATCH_1}")
                    set(_gentest_pp_rest "${CMAKE_MATCH_2}")
                    string(REGEX REPLACE "^[ \t]+" "" _gentest_pp_rest "${_gentest_pp_rest}")
                    string(REGEX REPLACE "[ \t]+$" "" _gentest_pp_rest "${_gentest_pp_rest}")
                else()
                    set(_gentest_pp_keyword "")
                    set(_gentest_pp_rest "")
                endif()

                if(_gentest_pp_keyword STREQUAL "if")
                    set(_gentest_parent_active ${_gentest_current_active})
                    _gentest_pp_eval_condition("${_gentest_pp_rest}" "${_gentest_source_dir}"
                        _gentest_eval_known _gentest_eval_value ${_gentest_include_dirs})
                    if(_gentest_parent_active AND _gentest_eval_known AND _gentest_eval_value)
                        set(_gentest_branch_active TRUE)
                    else()
                        set(_gentest_branch_active FALSE)
                    endif()
                    list(APPEND _gentest_if_parent_stack ${_gentest_parent_active})
                    list(APPEND _gentest_if_taken_stack ${_gentest_branch_active})
                    list(APPEND _gentest_if_active_stack ${_gentest_branch_active})
                    set(_gentest_current_active ${_gentest_branch_active})
                elseif(_gentest_pp_keyword STREQUAL "ifdef" OR _gentest_pp_keyword STREQUAL "ifndef")
                    set(_gentest_parent_active ${_gentest_current_active})
                    string(REGEX MATCH "^([A-Za-z_][A-Za-z0-9_]*)" _gentest_ifdef_ident "${_gentest_pp_rest}")
                    set(_gentest_pp_macro_var "_gentest_pp_macro_${_gentest_ifdef_ident}")
                    if(_gentest_ifdef_ident AND DEFINED ${_gentest_pp_macro_var})
                        set(_gentest_ident_defined TRUE)
                    else()
                        set(_gentest_ident_defined FALSE)
                    endif()
                    if(_gentest_parent_active AND ((_gentest_pp_keyword STREQUAL "ifdef" AND _gentest_ident_defined) OR
                        (_gentest_pp_keyword STREQUAL "ifndef" AND NOT _gentest_ident_defined)))
                        set(_gentest_branch_active TRUE)
                    else()
                        set(_gentest_branch_active FALSE)
                    endif()
                    list(APPEND _gentest_if_parent_stack ${_gentest_parent_active})
                    list(APPEND _gentest_if_taken_stack ${_gentest_branch_active})
                    list(APPEND _gentest_if_active_stack ${_gentest_branch_active})
                    set(_gentest_current_active ${_gentest_branch_active})
                elseif(_gentest_pp_keyword STREQUAL "elif")
                    list(LENGTH _gentest_if_parent_stack _gentest_if_depth)
                    if(_gentest_if_depth GREATER 0)
                        math(EXPR _gentest_if_last "${_gentest_if_depth} - 1")
                        list(GET _gentest_if_parent_stack ${_gentest_if_last} _gentest_parent_active)
                        list(GET _gentest_if_taken_stack ${_gentest_if_last} _gentest_branch_taken)
                        _gentest_pp_eval_condition("${_gentest_pp_rest}" "${_gentest_source_dir}"
                            _gentest_eval_known _gentest_eval_value ${_gentest_include_dirs})
                        if(_gentest_parent_active AND NOT _gentest_branch_taken AND _gentest_eval_known AND _gentest_eval_value)
                            set(_gentest_branch_active TRUE)
                        else()
                            set(_gentest_branch_active FALSE)
                        endif()
                        if(_gentest_branch_active)
                            list(REMOVE_AT _gentest_if_taken_stack ${_gentest_if_last})
                            list(APPEND _gentest_if_taken_stack TRUE)
                        endif()
                        list(REMOVE_AT _gentest_if_active_stack ${_gentest_if_last})
                        list(APPEND _gentest_if_active_stack ${_gentest_branch_active})
                        set(_gentest_current_active ${_gentest_branch_active})
                    endif()
                elseif(_gentest_pp_keyword STREQUAL "else")
                    list(LENGTH _gentest_if_parent_stack _gentest_if_depth)
                    if(_gentest_if_depth GREATER 0)
                        math(EXPR _gentest_if_last "${_gentest_if_depth} - 1")
                        list(GET _gentest_if_parent_stack ${_gentest_if_last} _gentest_parent_active)
                        list(GET _gentest_if_taken_stack ${_gentest_if_last} _gentest_branch_taken)
                        if(_gentest_parent_active AND NOT _gentest_branch_taken)
                            set(_gentest_branch_active TRUE)
                        else()
                            set(_gentest_branch_active FALSE)
                        endif()
                        list(REMOVE_AT _gentest_if_taken_stack ${_gentest_if_last})
                        list(APPEND _gentest_if_taken_stack TRUE)
                        list(REMOVE_AT _gentest_if_active_stack ${_gentest_if_last})
                        list(APPEND _gentest_if_active_stack ${_gentest_branch_active})
                        set(_gentest_current_active ${_gentest_branch_active})
                    endif()
                elseif(_gentest_pp_keyword STREQUAL "endif")
                    list(LENGTH _gentest_if_parent_stack _gentest_if_depth)
                    if(_gentest_if_depth GREATER 0)
                        math(EXPR _gentest_if_last "${_gentest_if_depth} - 1")
                        list(GET _gentest_if_parent_stack ${_gentest_if_last} _gentest_parent_active)
                        list(REMOVE_AT _gentest_if_parent_stack ${_gentest_if_last})
                        list(REMOVE_AT _gentest_if_taken_stack ${_gentest_if_last})
                        list(REMOVE_AT _gentest_if_active_stack ${_gentest_if_last})
                        set(_gentest_current_active ${_gentest_parent_active})
                    else()
                        set(_gentest_current_active TRUE)
                    endif()
                elseif(_gentest_current_active AND _gentest_pp_keyword STREQUAL "define")
                    if(_gentest_pp_rest MATCHES "^([A-Za-z_][A-Za-z0-9_]*)(.*)$")
                        set(_gentest_define_name "${CMAKE_MATCH_1}")
                        set(_gentest_define_tail "${CMAKE_MATCH_2}")
                        string(REGEX REPLACE "^[ \t]+" "" _gentest_define_tail "${_gentest_define_tail}")
                        if(NOT _gentest_define_tail MATCHES "^\\(")
                            set(_gentest_pp_macro_var "_gentest_pp_macro_${_gentest_define_name}")
                            set(${_gentest_pp_macro_var} "${_gentest_define_tail}")
                        endif()
                    endif()
                elseif(_gentest_current_active AND _gentest_pp_keyword STREQUAL "undef")
                    string(REGEX MATCH "^([A-Za-z_][A-Za-z0-9_]*)" _gentest_undef_name "${_gentest_pp_rest}")
                    if(_gentest_undef_name)
                        set(_gentest_pp_macro_var "_gentest_pp_macro_${_gentest_undef_name}")
                        unset(${_gentest_pp_macro_var})
                    endif()
                endif()
                set(_gentest_pp_buffer "")
            endif()
            continue()
        endif()

        if(NOT _gentest_current_active)
            continue()
        endif()
        if(_gentest_trimmed STREQUAL "")
            continue()
        endif()

        if(NOT _gentest_statement_buffer STREQUAL "")
            string(APPEND _gentest_statement_buffer " ")
        endif()
        string(APPEND _gentest_statement_buffer "${_gentest_trimmed}")

        string(FIND "${_gentest_statement_buffer}" ";" _gentest_stmt_end)
        while(NOT _gentest_stmt_end EQUAL -1 AND _gentest_module_name STREQUAL "")
            string(SUBSTRING "${_gentest_statement_buffer}" 0 ${_gentest_stmt_end} _gentest_stmt)
            math(EXPR _gentest_rest_start "${_gentest_stmt_end} + 1")
            string(LENGTH "${_gentest_statement_buffer}" _gentest_stmt_buffer_len)
            if(_gentest_rest_start LESS _gentest_stmt_buffer_len)
                math(EXPR _gentest_rest_len "${_gentest_stmt_buffer_len} - ${_gentest_rest_start}")
                string(SUBSTRING "${_gentest_statement_buffer}" ${_gentest_rest_start} ${_gentest_rest_len} _gentest_statement_buffer)
            else()
                set(_gentest_statement_buffer "")
            endif()

            string(REGEX REPLACE "^[ \t\r\n]+" "" _gentest_stmt "${_gentest_stmt}")
            string(REGEX REPLACE "[ \t\r\n]+$" "" _gentest_stmt "${_gentest_stmt}")
            if(_gentest_stmt MATCHES "^module[ \t]*$")
                string(FIND "${_gentest_statement_buffer}" ";" _gentest_stmt_end)
                continue()
            endif()
            if(_gentest_stmt MATCHES "^(export[ \t]+)?module[ \t]+([^;]+)[ \t]*$")
                _gentest_canonicalize_module_name("${CMAKE_MATCH_2}" FALSE _gentest_canonical_module_name)
                if(NOT "${_gentest_canonical_module_name}" STREQUAL "")
                    set(_gentest_module_name "${_gentest_canonical_module_name}")
                    break()
                endif()
            endif()

            string(FIND "${_gentest_statement_buffer}" ";" _gentest_stmt_end)
        endwhile()
    endwhile()

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
            list(APPEND _gentest_registration_cpp "__gentest_no_registration__")
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

function(_gentest_collect_scan_include_dirs target source out_dirs)
    set(_gentest_dirs "")

    foreach(_gentest_prop IN ITEMS INCLUDE_DIRECTORIES SYSTEM_INCLUDE_DIRECTORIES)
        get_target_property(_gentest_prop_values ${target} ${_gentest_prop})
        if(NOT _gentest_prop_values STREQUAL "NOTFOUND")
            foreach(_gentest_dir IN LISTS _gentest_prop_values)
                if("${_gentest_dir}" MATCHES "\\$<")
                    continue()
                endif()
                list(APPEND _gentest_dirs "${_gentest_dir}")
            endforeach()
        endif()
    endforeach()

    get_source_file_property(_gentest_source_include_dirs "${source}" INCLUDE_DIRECTORIES)
    if(NOT _gentest_source_include_dirs STREQUAL "NOTFOUND")
        foreach(_gentest_dir IN LISTS _gentest_source_include_dirs)
            if("${_gentest_dir}" MATCHES "\\$<")
                continue()
            endif()
            list(APPEND _gentest_dirs "${_gentest_dir}")
        endforeach()
    endif()

    set(_gentest_expect_include_dir FALSE)
    foreach(_gentest_arg IN LISTS ARGN)
        if(_gentest_expect_include_dir)
            set(_gentest_expect_include_dir FALSE)
            if(NOT "${_gentest_arg}" MATCHES "\\$<")
                list(APPEND _gentest_dirs "${_gentest_arg}")
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
                    list(APPEND _gentest_dirs "${_gentest_dir}")
                endif()
                break()
            endif()
        endforeach()
    endforeach()

    list(REMOVE_DUPLICATES _gentest_dirs)
    set(${out_dirs} "${_gentest_dirs}" PARENT_SCOPE)
endfunction()

function(_gentest_collect_codegen_dep_targets_from_link_graph target out_targets)
    set(_gentest_result "")
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

        get_target_property(_gentest_mock_codegen_dep "${_gentest_mock_target_actual}" GENTEST_CODEGEN_DEP_TARGET)
        if(_gentest_mock_codegen_dep AND NOT _gentest_mock_codegen_dep MATCHES "-NOTFOUND$")
            add_dependencies(${_gentest_consumer_codegen_dep} ${_gentest_mock_codegen_dep})
        endif()
        add_dependencies(${_gentest_consumer_codegen_dep} ${_gentest_mock_target_actual})
    endforeach()
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
        if(NOT _gentest_ext MATCHES "^\\.(cc|cpp|cxx|cppm|ccm|cxxm|ixx|mxx)$")
            continue()
        endif()
        _gentest_normalize_path_and_key("${_gentest_src}" "${CMAKE_CURRENT_SOURCE_DIR}" _gentest_src_abs _gentest_src_key)
        _gentest_collect_scan_include_dirs(${target} "${_gentest_src}" _gentest_scan_include_dirs ${GENTEST_CLANG_ARGS})
        _gentest_try_extract_module_name("${_gentest_src_abs}" _gentest_module_name ${_gentest_scan_include_dirs})
        set(_gentest_is_module FALSE)
        if(NOT _gentest_module_name STREQUAL "")
            set(_gentest_is_module TRUE)
        endif()
        list(APPEND _gentest_tu_source_entries "${_gentest_src}")
        list(APPEND _gentest_tus "${_gentest_src_abs}")
        if(_gentest_is_module)
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
    get_target_property(_gentest_attach_discovers_mocks ${target} GENTEST_EXPLICIT_MOCK_TARGET)
    if(_gentest_attach_discovers_mocks)
        list(APPEND _command --discover-mocks)
    endif()
    if(NOT "${GENTEST_CODEGEN_SCAN_DEPS_MODE}" STREQUAL "")
        list(APPEND _command "--scan-deps-mode=${GENTEST_CODEGEN_SCAN_DEPS_MODE}")
    endif()
    set(_gentest_clang_scan_deps "${GENTEST_CODEGEN_CLANG_SCAN_DEPS}")
    if("${_gentest_clang_scan_deps}" STREQUAL ""
       AND DEFINED CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS
       AND NOT "${CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS}" STREQUAL ""
       AND NOT "${CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS}" MATCHES "-NOTFOUND$")
        set(_gentest_clang_scan_deps "${CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS}")
    endif()
    if(NOT "${_gentest_clang_scan_deps}" STREQUAL "")
        list(APPEND _command --clang-scan-deps "${_gentest_clang_scan_deps}")
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
    _gentest_collect_codegen_dep_targets_from_link_graph(${target} _gentest_linked_codegen_dep_targets)
    if(_gentest_linked_codegen_dep_targets)
        list(APPEND _gentest_codegen_deps ${_gentest_linked_codegen_dep_targets})
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
            _gentest_try_extract_module_name("${_gentest_def_abs}" _gentest_module_name)
            if(_gentest_module_name STREQUAL "")
                message(FATAL_ERROR
                    "gentest_add_mocks(${target}): failed to extract a named module declaration from '${_gentest_def_abs}'. "
                    "Explicit mock module defs must declare `export module ...;`.")
            endif()
            list(APPEND _gentest_module_def_names "${_gentest_module_name}")
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
                if(NOT "${_gentest_link_include_dir}" STREQUAL "")
                    list(APPEND _gentest_explicit_mock_search_roots "${_gentest_link_include_dir}")
                endif()
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
        set(_gentest_aggregate_module "${_gentest_output_dir}/${_gentest_target_id}.cppm")
        set(_gentest_aggregate_module_content
"// This file is auto-generated by gentest (explicit mocks aggregate module).\n\
// Do not edit manually.\n\
\n\
module;\n\
\n\
export module ${GENTEST_MODULE_NAME};\n\
\n")
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
