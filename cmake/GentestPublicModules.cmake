function(gentest_resolve_optional_program out_program candidate)
    if("${candidate}" STREQUAL "")
        set(${out_program} "" PARENT_SCOPE)
        return()
    endif()

    if(EXISTS "${candidate}" AND NOT IS_DIRECTORY "${candidate}")
        set(${out_program} "${candidate}" PARENT_SCOPE)
        return()
    endif()

    get_filename_component(_candidate_name "${candidate}" NAME)
    if(NOT IS_ABSOLUTE "${candidate}" AND NOT "${candidate}" MATCHES "[/\\\\]")
        unset(_resolved_program CACHE)
        unset(_resolved_program)
        find_program(_resolved_program NAMES "${candidate}" "${_candidate_name}" NO_CACHE)
    endif()
    if(_resolved_program)
        set(${out_program} "${_resolved_program}" PARENT_SCOPE)
        return()
    endif()

    set(${out_program} "" PARENT_SCOPE)
endfunction()

function(gentest_detect_thread_sanitizer out_enabled)
    set(_probe
        "${CMAKE_CXX_FLAGS} ${CMAKE_EXE_LINKER_FLAGS} ${CMAKE_MODULE_LINKER_FLAGS} ${CMAKE_SHARED_LINKER_FLAGS}")

    if(CMAKE_BUILD_TYPE AND NOT "${CMAKE_BUILD_TYPE}" STREQUAL "")
        string(TOUPPER "${CMAKE_BUILD_TYPE}" _build_type_upper)
        foreach(_flag_var
                IN ITEMS
                    CMAKE_CXX_FLAGS_${_build_type_upper}
                    CMAKE_EXE_LINKER_FLAGS_${_build_type_upper}
                    CMAKE_MODULE_LINKER_FLAGS_${_build_type_upper}
                    CMAKE_SHARED_LINKER_FLAGS_${_build_type_upper})
            if(DEFINED ${_flag_var} AND NOT "${${_flag_var}}" STREQUAL "")
                string(APPEND _probe " ${${_flag_var}}")
            endif()
        endforeach()
    endif()

    if(_probe MATCHES "(^|[ ;])-fsanitize=[^ ;]*thread[^ ;]*([ ;]|$)")
        set(${out_enabled} TRUE PARENT_SCOPE)
    else()
        set(${out_enabled} FALSE PARENT_SCOPE)
    endif()
endfunction()

function(gentest_detect_public_module_support out_supported out_reason)
    set(_supported TRUE)
    set(_reason "")

    if(CMAKE_GENERATOR STREQUAL "Ninja" OR CMAKE_GENERATOR STREQUAL "Ninja Multi-Config")
        if(NOT DEFINED CMAKE_MAKE_PROGRAM OR "${CMAKE_MAKE_PROGRAM}" STREQUAL "")
            set(_supported FALSE)
            set(_reason "CMAKE_MAKE_PROGRAM is empty for Ninja-based generator '${CMAKE_GENERATOR}'")
        else()
            execute_process(
                COMMAND "${CMAKE_MAKE_PROGRAM}" --version
                RESULT_VARIABLE _ninja_rc
                OUTPUT_VARIABLE _ninja_out
                ERROR_VARIABLE _ninja_err
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_STRIP_TRAILING_WHITESPACE)
            if(NOT _ninja_rc EQUAL 0)
                set(_supported FALSE)
                set(_reason "failed to query Ninja version from '${CMAKE_MAKE_PROGRAM}': ${_ninja_err}")
            else()
                string(REGEX MATCH "[0-9]+\\.[0-9]+(\\.[0-9]+)?" _ninja_version "${_ninja_out}")
                if(NOT _ninja_version)
                    set(_supported FALSE)
                    set(_reason "unable to parse Ninja version from '${CMAKE_MAKE_PROGRAM} --version' output '${_ninja_out}'")
                elseif(_ninja_version VERSION_LESS 1.11)
                    set(_supported FALSE)
                    set(_reason "Ninja ${_ninja_version} is too old; Ninja 1.11 or newer is required")
                endif()
            endif()
        endif()
    else()
        set(_supported FALSE)
        set(_reason "generator '${CMAKE_GENERATOR}' does not support gentest's public module export/import path; use Ninja 1.11+")
    endif()

    if(_supported
       AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
       AND DEFINED CMAKE_CXX_COMPILER_VERSION
       AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 15)
        set(_supported FALSE)
        set(_reason "GNU ${CMAKE_CXX_COMPILER_VERSION} is too old; GNU 15 or newer is required for public named modules")
    endif()

    if(_supported
       AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
       AND DEFINED CMAKE_CXX_COMPILER_VERSION
       AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16)
        gentest_detect_thread_sanitizer(_gentest_thread_sanitizer_enabled)
        if(_gentest_thread_sanitizer_enabled)
            set(_supported FALSE)
            set(_reason
                "GNU ${CMAKE_CXX_COMPILER_VERSION} with ThreadSanitizer cannot build gentest's public named modules; GNU 16 or newer is required when -fsanitize=thread is enabled")
        endif()
    endif()

    if(_supported)
        if(NOT DEFINED CMAKE_CXX_SCANDEP_SOURCE OR "${CMAKE_CXX_SCANDEP_SOURCE}" STREQUAL "")
            set(_supported FALSE)
            set(_reason
                "compiler '${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}' does not provide C++ module dependency scanning for generator '${CMAKE_GENERATOR}'")
        elseif(CMAKE_CXX_COMPILER_ID MATCHES "^(AppleClang|Clang)$"
               AND DEFINED CMAKE_CXX_COMPILER_FRONTEND_VARIANT
               AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
            set(_resolved_scan_deps "")
            if(DEFINED CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS
               AND NOT "${CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS}" STREQUAL ""
               AND NOT "${CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS}" MATCHES "-NOTFOUND$")
                gentest_resolve_optional_program(_resolved_scan_deps "${CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS}")
            endif()
            if("${_resolved_scan_deps}" STREQUAL "")
                set(_supported FALSE)
                set(_reason
                    "clang dependency scanner was not found for '${CMAKE_CXX_COMPILER}'; set CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS to a usable clang-scan-deps binary")
            endif()
        endif()
    endif()

    set(${out_supported} "${_supported}" PARENT_SCOPE)
    set(${out_reason} "${_reason}" PARENT_SCOPE)
endfunction()
