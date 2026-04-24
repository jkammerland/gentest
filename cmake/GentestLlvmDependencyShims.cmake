# Compatibility targets and import repairs for LLVM/Clang package exports.

function(gentest_ensure_llvm_zstd_targets)
    find_package(zstd CONFIG QUIET)

    if(NOT TARGET zstd::libzstd)
        find_library(_gentest_zstd_library
            NAMES zstd libzstd.so.1 libzstd.so zstd_static
            PATHS
                /usr/lib64
                /usr/lib
                /lib64
                /lib
                /usr/lib/x86_64-linux-gnu
                /lib/x86_64-linux-gnu)
        if(_gentest_zstd_library)
            add_library(zstd::libzstd UNKNOWN IMPORTED)
            set_target_properties(zstd::libzstd PROPERTIES
                IMPORTED_LOCATION "${_gentest_zstd_library}")
        endif()
    endif()

    if(TARGET zstd::libzstd)
        foreach(_gentest_zstd_alias IN ITEMS zstd::libzstd_shared zstd::libzstd_static)
            if(NOT TARGET "${_gentest_zstd_alias}")
                add_library("${_gentest_zstd_alias}" INTERFACE IMPORTED)
                set_target_properties("${_gentest_zstd_alias}" PROPERTIES
                    INTERFACE_LINK_LIBRARIES zstd::libzstd)
            endif()
        endforeach()
    elseif(TARGET zstd::libzstd_shared AND NOT TARGET zstd::libzstd_static)
        add_library(zstd::libzstd_static INTERFACE IMPORTED)
        set_target_properties(zstd::libzstd_static PROPERTIES
            INTERFACE_LINK_LIBRARIES zstd::libzstd_shared)
    elseif(TARGET zstd::libzstd_static AND NOT TARGET zstd::libzstd_shared)
        add_library(zstd::libzstd_shared INTERFACE IMPORTED)
        set_target_properties(zstd::libzstd_shared PROPERTIES
            INTERFACE_LINK_LIBRARIES zstd::libzstd_static)
    endif()
endfunction()

macro(gentest_seed_llvm_prefix_from_config)
    set(_gentest_llvm_config_candidates llvm-config)
    foreach(_v 22 21 20 19 18)
        list(APPEND _gentest_llvm_config_candidates "llvm-config-${_v}")
    endforeach()
    foreach(_cand IN LISTS _gentest_llvm_config_candidates)
        execute_process(
            COMMAND ${_cand} --cmakedir
            OUTPUT_VARIABLE _gentest_llvm_cmakedir
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        if(_gentest_llvm_cmakedir AND EXISTS "${_gentest_llvm_cmakedir}/LLVMConfig.cmake")
            get_filename_component(_gentest_llvm_prefix "${_gentest_llvm_cmakedir}/../.." ABSOLUTE)
            list(PREPEND CMAKE_PREFIX_PATH "${_gentest_llvm_prefix}")
            set(GENTEST_LLVM_DETECTED_PREFIX "${_gentest_llvm_prefix}" CACHE INTERNAL
                "LLVM prefix detected from llvm-config" FORCE)
            break()
        endif()
    endforeach()
    unset(_gentest_llvm_config_candidates)
endmacro()

function(gentest_ensure_llvm_terminfo_target)
    if(NOT DEFINED GENTEST_USE_SYSTEM_TERMINFO)
        set(_gentest_use_system_terminfo_default OFF)
        if(UNIX AND NOT APPLE AND DEFINED GENTEST_LLVM_DETECTED_PREFIX)
            set(_gentest_llvm_probe_dirs
                "${GENTEST_LLVM_DETECTED_PREFIX}"
                "${GENTEST_LLVM_DETECTED_PREFIX}/lib"
                "${GENTEST_LLVM_DETECTED_PREFIX}/lib64"
                "${GENTEST_LLVM_DETECTED_PREFIX}/lib/x86_64-linux-gnu")
            list(REMOVE_DUPLICATES _gentest_llvm_probe_dirs)

            set(_gentest_llvm_probe_candidates "")
            foreach(_dir IN LISTS _gentest_llvm_probe_dirs)
                file(GLOB _gentest_found
                    LIST_DIRECTORIES FALSE
                    "${_dir}/libLLVM*.so*" "${_dir}/libLLVM-*.so*")
                list(APPEND _gentest_llvm_probe_candidates ${_gentest_found})
            endforeach()

            if(_gentest_llvm_probe_candidates)
                list(GET _gentest_llvm_probe_candidates 0 _gentest_llvm_probe)
                find_program(_gentest_ldd ldd)
                set(_gentest_needs_tinfo5 TRUE)
                if(_gentest_ldd)
                    execute_process(
                        COMMAND ${_gentest_ldd} "${_gentest_llvm_probe}"
                        RESULT_VARIABLE _gentest_ldd_rc
                        OUTPUT_VARIABLE _gentest_ldd_out
                        ERROR_VARIABLE _gentest_ldd_err
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_STRIP_TRAILING_WHITESPACE)
                    if(_gentest_ldd_rc EQUAL 0)
                        if(NOT _gentest_ldd_out MATCHES "libtinfo\\.so\\.5")
                            set(_gentest_needs_tinfo5 FALSE)
                        endif()
                    endif()
                endif()
                if(NOT _gentest_needs_tinfo5)
                    set(_gentest_use_system_terminfo_default ON)
                endif()
            endif()
        endif()

        set(GENTEST_USE_SYSTEM_TERMINFO "${_gentest_use_system_terminfo_default}" CACHE BOOL
            "Link against the host-provided Terminfo package instead of the bundled shim")
    endif()

    if(TARGET Terminfo::terminfo)
        return()
    endif()

    set(_gentest_terminfo_links "")
    set(_gentest_terminfo_includes "")
    set(_gentest_use_local_tinfo_shim FALSE)
    if(GENTEST_USE_SYSTEM_TERMINFO AND UNIX)
        set(_gentest_terminfo_candidates)
        set(_gentest_terminfo_search_dirs
            /usr/lib64 /usr/lib /lib64 /lib
            /usr/lib/x86_64-linux-gnu
            /lib/x86_64-linux-gnu)
        if(DEFINED GENTEST_LLVM_DETECTED_PREFIX)
            list(APPEND _gentest_terminfo_search_dirs "${GENTEST_LLVM_DETECTED_PREFIX}/lib")
        endif()
        if(CMAKE_C_COMPILER)
            get_filename_component(_gentest_cc_dir "${CMAKE_C_COMPILER}" DIRECTORY)
            if(_gentest_cc_dir)
                get_filename_component(_gentest_cc_root "${_gentest_cc_dir}/.." ABSOLUTE)
                list(APPEND _gentest_terminfo_search_dirs "${_gentest_cc_root}/lib")
            endif()
        endif()
        list(REMOVE_DUPLICATES _gentest_terminfo_search_dirs)

        foreach(_gentest_dir IN LISTS _gentest_terminfo_search_dirs)
            file(GLOB _gentest_found
                LIST_DIRECTORIES FALSE
                "${_gentest_dir}/libtinfo.so.6*" "${_gentest_dir}/libtinfo6.so*"
                "${_gentest_dir}/libtinfo.so.5*" "${_gentest_dir}/libtinfo5.so*")
            list(APPEND _gentest_terminfo_candidates ${_gentest_found})
        endforeach()
        if(_gentest_terminfo_candidates)
            list(GET _gentest_terminfo_candidates 0 _gentest_best_terminfo)
            list(APPEND _gentest_terminfo_links "${_gentest_best_terminfo}")
        endif()
    endif()
    if(GENTEST_USE_SYSTEM_TERMINFO AND _gentest_terminfo_links STREQUAL "")
        find_package(Terminfo QUIET)
        if(Terminfo_FOUND AND Terminfo_LIBRARIES)
            foreach(_gentest_candidate IN LISTS Terminfo_LIBRARIES)
                if(_gentest_candidate MATCHES "libtinfo\\.so\\.5(\\..*)?$")
                    list(APPEND _gentest_terminfo_links "${_gentest_candidate}")
                endif()
            endforeach()
            if(_gentest_terminfo_links STREQUAL "" AND Terminfo_LIBRARIES)
                list(APPEND _gentest_terminfo_links "${Terminfo_LIBRARIES}")
            endif()
            if(_gentest_terminfo_includes STREQUAL "" AND DEFINED Terminfo_INCLUDE_DIRS)
                set(_gentest_terminfo_includes "${Terminfo_INCLUDE_DIRS}")
            endif()
        endif()
    endif()
    if(NOT GENTEST_USE_SYSTEM_TERMINFO)
        if(UNIX)
            set(_gentest_use_local_tinfo_shim TRUE)
        endif()
    elseif(_gentest_terminfo_links STREQUAL "" AND UNIX)
        set(_gentest_use_local_tinfo_shim TRUE)
    endif()

    add_library(Terminfo::terminfo INTERFACE IMPORTED)
    if(APPLE AND _gentest_use_local_tinfo_shim)
        find_library(_gentest_ncurses_lib NAMES ncursesw ncurses curses)
        if(_gentest_ncurses_lib)
            list(APPEND _gentest_terminfo_links "${_gentest_ncurses_lib}")
        else()
            list(APPEND _gentest_terminfo_links "-lncurses")
        endif()
        set(_gentest_use_local_tinfo_shim FALSE)
    endif()

    if(_gentest_use_local_tinfo_shim AND UNIX)
        add_library(gentest_tinfo_shim SHARED
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tools/src/terminfo_shim.cpp")
        target_compile_features(gentest_tinfo_shim PRIVATE cxx_std_20)
        target_link_libraries(gentest_tinfo_shim PRIVATE dl)
        set_target_properties(gentest_tinfo_shim PROPERTIES
            OUTPUT_NAME tinfo
            SOVERSION 5
            VERSION 5
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
        target_link_options(gentest_tinfo_shim PRIVATE
            "-Wl,--version-script=${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tools/terminfo_shim.map")
        list(APPEND _gentest_terminfo_links gentest_tinfo_shim)
        set_property(TARGET gentest_tinfo_shim PROPERTY BUILD_RPATH "${CMAKE_CURRENT_BINARY_DIR}")
        set(GENTEST_TERMINFO_SHIM_DIR "${CMAKE_CURRENT_BINARY_DIR}" CACHE INTERNAL
            "Directory containing the Gentest Terminfo shim" FORCE)
        set(GENTEST_USES_TERMINFO_SHIM ON CACHE INTERNAL
            "Whether Gentest links against the bundled Terminfo shim" FORCE)
    else()
        set(GENTEST_TERMINFO_SHIM_DIR "" CACHE INTERNAL
            "Directory containing the Gentest Terminfo shim" FORCE)
        set(GENTEST_USES_TERMINFO_SHIM OFF CACHE INTERNAL
            "Whether Gentest links against the bundled Terminfo shim" FORCE)
    endif()
    if(_gentest_terminfo_links)
        set_target_properties(Terminfo::terminfo PROPERTIES
            INTERFACE_LINK_LIBRARIES "${_gentest_terminfo_links}")
    endif()
    if(_gentest_terminfo_includes)
        set_property(TARGET Terminfo::terminfo
            PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${_gentest_terminfo_includes}")
    endif()
endfunction()

macro(gentest_prepare_llvm_package_imports)
    set(CMAKE_DISABLE_FIND_PACKAGE_LibEdit ON)
    gentest_seed_llvm_prefix_from_config()
    gentest_ensure_llvm_zstd_targets()
    gentest_ensure_llvm_terminfo_target()
endmacro()

function(gentest_patch_llvm_missing_diaguids)
    if(NOT WIN32)
        return()
    endif()

    file(GLOB _gentest_diaguids_candidates
        LIST_DIRECTORIES FALSE
        "C:/Program Files/Microsoft Visual Studio/2022/*/DIA SDK/lib/amd64/diaguids.lib"
        "C:/Program Files (x86)/Microsoft Visual Studio/2022/*/DIA SDK/lib/amd64/diaguids.lib"
        "C:/Program Files/Microsoft Visual Studio/2019/*/DIA SDK/lib/amd64/diaguids.lib"
        "C:/Program Files (x86)/Microsoft Visual Studio/2019/*/DIA SDK/lib/amd64/diaguids.lib")
    if(_gentest_diaguids_candidates)
        list(GET _gentest_diaguids_candidates 0 _gentest_diaguids_lib)
    endif()

    foreach(_gentest_target IN LISTS ARGN)
        if(NOT TARGET "${_gentest_target}")
            continue()
        endif()
        get_target_property(_gentest_link_libs "${_gentest_target}" INTERFACE_LINK_LIBRARIES)
        if(NOT _gentest_link_libs OR _gentest_link_libs MATCHES "-NOTFOUND$")
            continue()
        endif()

        set(_gentest_patched_link_libs)
        set(_gentest_patched FALSE)
        foreach(_gentest_lib IN LISTS _gentest_link_libs)
            if(_gentest_lib MATCHES "(^|[/\\\\])diaguids\\.lib$" AND NOT EXISTS "${_gentest_lib}" AND DEFINED _gentest_diaguids_lib)
                list(APPEND _gentest_patched_link_libs "${_gentest_diaguids_lib}")
                set(_gentest_patched TRUE)
            else()
                list(APPEND _gentest_patched_link_libs "${_gentest_lib}")
            endif()
        endforeach()

        if(_gentest_patched)
            set_property(TARGET "${_gentest_target}" PROPERTY INTERFACE_LINK_LIBRARIES "${_gentest_patched_link_libs}")
            message(STATUS "gentest: patched ${_gentest_target} diaguids.lib: ${_gentest_diaguids_lib}")
        endif()
    endforeach()
endfunction()

function(gentest_imported_artifacts_exist out_var target_name)
    set(_ok TRUE)
    if(TARGET "${target_name}")
        get_target_property(_imported "${target_name}" IMPORTED)
        if(_imported)
            set(_artifact_props IMPORTED_LOCATION IMPORTED_IMPLIB)
            get_target_property(_imported_configs "${target_name}" IMPORTED_CONFIGURATIONS)
            if(_imported_configs AND NOT _imported_configs MATCHES "-NOTFOUND$")
                foreach(_config IN LISTS _imported_configs)
                    list(APPEND _artifact_props "IMPORTED_LOCATION_${_config}" "IMPORTED_IMPLIB_${_config}")
                endforeach()
            endif()

            foreach(_prop IN LISTS _artifact_props)
                get_target_property(_artifact "${target_name}" "${_prop}")
                if(_artifact AND NOT _artifact MATCHES "-NOTFOUND$" AND IS_ABSOLUTE "${_artifact}" AND NOT EXISTS "${_artifact}")
                    message(STATUS "gentest: skipping ${target_name}; ${_prop} does not exist: ${_artifact}")
                    set(_ok FALSE)
                    break()
                endif()
            endforeach()
        endif()
    endif()
    set(${out_var} "${_ok}" PARENT_SCOPE)
endfunction()

function(gentest_get_imported_config_artifacts target config out_location out_implib)
    string(TOUPPER "${config}" _gentest_config_upper)
    if(_gentest_config_upper STREQUAL "")
        get_target_property(_gentest_location "${target}" IMPORTED_LOCATION)
        get_target_property(_gentest_implib "${target}" IMPORTED_IMPLIB)
    else()
        get_target_property(_gentest_location "${target}" "IMPORTED_LOCATION_${_gentest_config_upper}")
        get_target_property(_gentest_implib "${target}" "IMPORTED_IMPLIB_${_gentest_config_upper}")
    endif()
    if(NOT _gentest_location OR _gentest_location MATCHES "-NOTFOUND$")
        set(_gentest_location "")
    endif()
    if(NOT _gentest_implib OR _gentest_implib MATCHES "-NOTFOUND$")
        set(_gentest_implib "")
    endif()
    set(${out_location} "${_gentest_location}" PARENT_SCOPE)
    set(${out_implib} "${_gentest_implib}" PARENT_SCOPE)
endfunction()

function(gentest_ensure_imported_config_artifacts target config)
    string(TOUPPER "${config}" _gentest_config_upper)
    gentest_get_imported_config_artifacts("${target}" "${_gentest_config_upper}"
        _gentest_existing_location _gentest_existing_implib)
    if(_gentest_existing_location OR _gentest_existing_implib)
        return()
    endif()

    set(_gentest_fallback_configs RELEASE RELWITHDEBINFO MINSIZEREL NOCONFIG "")
    foreach(_gentest_fallback_config IN LISTS _gentest_fallback_configs)
        string(TOUPPER "${_gentest_fallback_config}" _gentest_fallback_upper)
        if(_gentest_fallback_upper STREQUAL _gentest_config_upper)
            continue()
        endif()

        gentest_get_imported_config_artifacts("${target}" "${_gentest_fallback_upper}"
            _gentest_fallback_location _gentest_fallback_implib)
        if(NOT _gentest_fallback_location AND NOT _gentest_fallback_implib)
            continue()
        endif()

        if(_gentest_fallback_location)
            set_property(TARGET "${target}" PROPERTY
                "IMPORTED_LOCATION_${_gentest_config_upper}" "${_gentest_fallback_location}")
        endif()
        if(_gentest_fallback_implib)
            set_property(TARGET "${target}" PROPERTY
                "IMPORTED_IMPLIB_${_gentest_config_upper}" "${_gentest_fallback_implib}")
        endif()
        get_target_property(_gentest_imported_configs "${target}" IMPORTED_CONFIGURATIONS)
        if(NOT _gentest_imported_configs OR _gentest_imported_configs MATCHES "-NOTFOUND$")
            set(_gentest_imported_configs "")
        endif()
        list(APPEND _gentest_imported_configs "${_gentest_config_upper}")
        list(REMOVE_DUPLICATES _gentest_imported_configs)
        set_property(TARGET "${target}" PROPERTY IMPORTED_CONFIGURATIONS "${_gentest_imported_configs}")
        message(STATUS "gentest: patched ${target} ${_gentest_config_upper} imported artifacts from ${_gentest_fallback_upper}")
        return()
    endforeach()
endfunction()
