include_guard(GLOBAL)

function(_gentest_qemu_cross_set_runtime_root_from_loader loader_name loader_path out_var)
    set(_root "")
    if(loader_path
       AND NOT loader_path STREQUAL "${loader_name}"
       AND IS_ABSOLUTE "${loader_path}"
       AND EXISTS "${loader_path}")
        get_filename_component(_loader_dir "${loader_path}" DIRECTORY)
        set(_best_match_depth -1)
        foreach(_suffix IN LISTS ARGN)
            if(_suffix STREQUAL "")
                set(_suffix_matches TRUE)
                set(_suffix_depth 0)
                set(_candidate_root "${_loader_dir}")
            else()
                string(REPLACE "/" ";" _suffix_parts "${_suffix}")
                list(LENGTH _suffix_parts _suffix_depth)
                list(REVERSE _suffix_parts)

                set(_suffix_matches TRUE)
                set(_candidate_root "${_loader_dir}")
                foreach(_suffix_part IN LISTS _suffix_parts)
                    get_filename_component(_candidate_name "${_candidate_root}" NAME)
                    if(NOT _candidate_name STREQUAL "${_suffix_part}")
                        set(_suffix_matches FALSE)
                        break()
                    endif()
                    get_filename_component(_candidate_root "${_candidate_root}" DIRECTORY)
                endforeach()
                unset(_suffix_parts)
            endif()

            if(_suffix_matches AND _suffix_depth GREATER _best_match_depth)
                set(_best_match_depth "${_suffix_depth}")
                get_filename_component(_root "${_candidate_root}" REALPATH)
            endif()
            unset(_candidate_root)
            unset(_suffix_matches)
            unset(_suffix_depth)
        endforeach()
        unset(_best_match_depth)

        if(_root STREQUAL "")
            set(_candidate_root "${_loader_dir}")
            while(TRUE)
                _gentest_qemu_cross_root_has_loader(
                    "${_candidate_root}" "${loader_name}" _gentest_candidate_has_loader ${ARGN})
                if(_gentest_candidate_has_loader)
                    get_filename_component(_root "${_candidate_root}" REALPATH)
                    unset(_gentest_candidate_has_loader)
                    break()
                endif()
                unset(_gentest_candidate_has_loader)

                get_filename_component(_parent_root "${_candidate_root}" DIRECTORY)
                if(_parent_root STREQUAL "${_candidate_root}")
                    break()
                endif()
                set(_candidate_root "${_parent_root}")
                unset(_parent_root)
            endwhile()
            unset(_candidate_root)
        endif()
        unset(_loader_dir)
    endif()
    set(${out_var} "${_root}" PARENT_SCOPE)
endfunction()

function(_gentest_qemu_cross_root_has_loader root_path loader_name out_var)
    set(_has_loader FALSE)
    if(root_path AND IS_DIRECTORY "${root_path}")
        foreach(_suffix IN LISTS ARGN)
            if(_suffix STREQUAL "")
                set(_candidate "${root_path}/${loader_name}")
            else()
                set(_candidate "${root_path}/${_suffix}/${loader_name}")
            endif()
            if(EXISTS "${_candidate}")
                set(_has_loader TRUE)
                break()
            endif()
        endforeach()
    endif()
    set(${out_var} ${_has_loader} PARENT_SCOPE)
endfunction()

function(_gentest_qemu_cross_root_has_headers root_path out_var)
    set(_has_headers FALSE)
    if(root_path AND IS_DIRECTORY "${root_path}")
        foreach(_header_probe IN LISTS ARGN)
            if(EXISTS "${root_path}/${_header_probe}")
                set(_has_headers TRUE)
                break()
            endif()
        endforeach()
    endif()
    set(${out_var} ${_has_headers} PARENT_SCOPE)
endfunction()

macro(gentest_configure_gnu_qemu_cross_toolchain)
    set(_options "")
    set(_one_value_args SYSTEM_PROCESSOR TARGET_TRIPLE LOADER_NAME)
    set(_multi_value_args QEMU_NAMES LOADER_DIR_SUFFIXES CANDIDATE_ROOTS HEADER_PROBES)
    cmake_parse_arguments(_gentest_qemu "${_options}" "${_one_value_args}" "${_multi_value_args}" ${ARGN})

    if(NOT _gentest_qemu_SYSTEM_PROCESSOR)
        message(FATAL_ERROR "gentest_configure_gnu_qemu_cross_toolchain: SYSTEM_PROCESSOR is required")
    endif()
    if(NOT _gentest_qemu_TARGET_TRIPLE)
        message(FATAL_ERROR "gentest_configure_gnu_qemu_cross_toolchain: TARGET_TRIPLE is required")
    endif()
    if(NOT _gentest_qemu_LOADER_NAME)
        message(FATAL_ERROR "gentest_configure_gnu_qemu_cross_toolchain: LOADER_NAME is required")
    endif()
    if(NOT _gentest_qemu_QEMU_NAMES)
        message(FATAL_ERROR "gentest_configure_gnu_qemu_cross_toolchain: at least one QEMU_NAMES entry is required")
    endif()
    if(NOT _gentest_qemu_LOADER_DIR_SUFFIXES)
        message(FATAL_ERROR "gentest_configure_gnu_qemu_cross_toolchain: LOADER_DIR_SUFFIXES is required")
    endif()
    if(NOT _gentest_qemu_HEADER_PROBES)
        set(_gentest_qemu_HEADER_PROBES "usr/include/stdio.h")
    endif()

    set(CMAKE_SYSTEM_NAME Linux)
    set(CMAKE_SYSTEM_PROCESSOR "${_gentest_qemu_SYSTEM_PROCESSOR}")

    if(NOT DEFINED CMAKE_C_COMPILER)
        set(CMAKE_C_COMPILER "${_gentest_qemu_TARGET_TRIPLE}-gcc")
    endif()

    if(NOT DEFINED CMAKE_CXX_COMPILER)
        set(CMAKE_CXX_COMPILER "${_gentest_qemu_TARGET_TRIPLE}-g++")
    endif()

    if(NOT DEFINED CMAKE_C_COMPILER_TARGET)
        set(CMAKE_C_COMPILER_TARGET "${_gentest_qemu_TARGET_TRIPLE}")
    endif()

    if(NOT DEFINED CMAKE_CXX_COMPILER_TARGET)
        set(CMAKE_CXX_COMPILER_TARGET "${_gentest_qemu_TARGET_TRIPLE}")
    endif()

    set(_gentest_qemu_sysroot "")
    execute_process(
        COMMAND ${CMAKE_C_COMPILER} -print-file-name=${_gentest_qemu_LOADER_NAME}
        OUTPUT_VARIABLE _gentest_loader
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    _gentest_qemu_cross_set_runtime_root_from_loader(
        "${_gentest_qemu_LOADER_NAME}" "${_gentest_loader}" _gentest_qemu_sysroot ${_gentest_qemu_LOADER_DIR_SUFFIXES})
    unset(_gentest_loader)

    if(NOT DEFINED CMAKE_SYSROOT OR CMAKE_SYSROOT STREQUAL "")
        execute_process(
            COMMAND ${CMAKE_C_COMPILER} -print-sysroot
            OUTPUT_VARIABLE _gentest_sysroot_from_compiler
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)

        # Avoid forcing incomplete multiarch prefixes as CMAKE_SYSROOT. Some
        # distro cross compilers report a runtime root that lacks full sysroot
        # headers, which can break the compiler's built-in search paths.
        if(_gentest_sysroot_from_compiler
           AND EXISTS "${_gentest_sysroot_from_compiler}"
           AND NOT _gentest_sysroot_from_compiler STREQUAL "/")
            get_filename_component(_gentest_sysroot_from_compiler "${_gentest_sysroot_from_compiler}" REALPATH)
            _gentest_qemu_cross_root_has_headers(
                "${_gentest_sysroot_from_compiler}" _gentest_sysroot_has_headers ${_gentest_qemu_HEADER_PROBES})
            if(_gentest_sysroot_has_headers)
                set(CMAKE_SYSROOT "${_gentest_sysroot_from_compiler}")
            endif()

            if(NOT _gentest_qemu_sysroot)
                _gentest_qemu_cross_root_has_loader(
                    "${_gentest_sysroot_from_compiler}"
                    "${_gentest_qemu_LOADER_NAME}"
                    _gentest_sysroot_has_loader
                    ${_gentest_qemu_LOADER_DIR_SUFFIXES})
                if(_gentest_sysroot_has_loader)
                    set(_gentest_qemu_sysroot "${_gentest_sysroot_from_compiler}")
                endif()
                unset(_gentest_sysroot_has_loader)
            endif()

            unset(_gentest_sysroot_has_headers)
        endif()
        unset(_gentest_sysroot_from_compiler)
    endif()

    set(_gentest_candidate_roots "/usr/${CMAKE_C_COMPILER_TARGET}" ${_gentest_qemu_CANDIDATE_ROOTS})
    list(REMOVE_DUPLICATES _gentest_candidate_roots)
    if(NOT _gentest_qemu_sysroot)
        foreach(_gentest_candidate_root IN LISTS _gentest_candidate_roots)
            _gentest_qemu_cross_root_has_loader(
                "${_gentest_candidate_root}"
                "${_gentest_qemu_LOADER_NAME}"
                _gentest_candidate_has_loader
                ${_gentest_qemu_LOADER_DIR_SUFFIXES})
            if(_gentest_candidate_has_loader)
                get_filename_component(_gentest_qemu_sysroot "${_gentest_candidate_root}" REALPATH)
                break()
            endif()
            unset(_gentest_candidate_has_loader)
        endforeach()
    endif()
    unset(_gentest_candidate_roots)

    if(DEFINED CMAKE_SYSROOT AND NOT CMAKE_SYSROOT STREQUAL "")
        set(CMAKE_FIND_ROOT_PATH "${CMAKE_SYSROOT}")
    elseif(_gentest_qemu_sysroot AND EXISTS "${_gentest_qemu_sysroot}")
        set(CMAKE_FIND_ROOT_PATH "${_gentest_qemu_sysroot}")
    endif()

    set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

    if(NOT DEFINED CMAKE_CROSSCOMPILING_EMULATOR OR CMAKE_CROSSCOMPILING_EMULATOR STREQUAL "")
        find_program(_gentest_qemu_emulator NAMES ${_gentest_qemu_QEMU_NAMES})
        if(_gentest_qemu_emulator AND _gentest_qemu_sysroot AND EXISTS "${_gentest_qemu_sysroot}")
            set(CMAKE_CROSSCOMPILING_EMULATOR "${_gentest_qemu_emulator};-L;${_gentest_qemu_sysroot}")
        elseif(_gentest_qemu_emulator AND DEFINED CMAKE_SYSROOT AND NOT CMAKE_SYSROOT STREQUAL "")
            set(CMAKE_CROSSCOMPILING_EMULATOR "${_gentest_qemu_emulator};-L;${CMAKE_SYSROOT}")
        endif()
        unset(_gentest_qemu_emulator)
    endif()

    unset(_gentest_qemu_sysroot)
    unset(_gentest_qemu)
    unset(_multi_value_args)
    unset(_one_value_args)
    unset(_options)
endmacro()
