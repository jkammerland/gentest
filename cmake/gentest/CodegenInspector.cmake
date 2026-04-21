include_guard(GLOBAL)

function(_gentest_file_imports_gentest_mock input out_contains)
    _gentest_run_source_inspector("${input}" _gentest_module_name _gentest_imports_gentest_mock ${ARGN})
    set(${out_contains} "${_gentest_imports_gentest_mock}" PARENT_SCOPE)
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
