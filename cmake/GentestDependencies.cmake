include_guard(GLOBAL)

function(_gentest_resolve_fmt_target out_var)
    if(TARGET fmt::fmt)
        get_target_property(_gentest_fmt_aliased_target fmt::fmt ALIASED_TARGET)
        if(_gentest_fmt_aliased_target AND NOT _gentest_fmt_aliased_target MATCHES "-NOTFOUND$")
            set(${out_var} "${_gentest_fmt_aliased_target}" PARENT_SCOPE)
            return()
        endif()
        set(${out_var} "fmt::fmt" PARENT_SCOPE)
        return()
    endif()

    if(TARGET fmt)
        set(${out_var} "fmt" PARENT_SCOPE)
        return()
    endif()

    set(${out_var} "" PARENT_SCOPE)
endfunction()

function(_gentest_cache_fmt_version)
    set(_gentest_fmt_version "")
    if(DEFINED fmt_VERSION AND NOT "${fmt_VERSION}" STREQUAL "")
        set(_gentest_fmt_version "${fmt_VERSION}")
    elseif(DEFINED fmt_VERSION_STRING AND NOT "${fmt_VERSION_STRING}" STREQUAL "")
        set(_gentest_fmt_version "${fmt_VERSION_STRING}")
    else()
        _gentest_resolve_fmt_target(_gentest_fmt_version_target)
        if(NOT "${_gentest_fmt_version_target}" STREQUAL "")
            get_target_property(_gentest_fmt_version "${_gentest_fmt_version_target}" VERSION)
            if(NOT _gentest_fmt_version OR _gentest_fmt_version MATCHES "-NOTFOUND$")
                set(_gentest_fmt_version "")
            endif()
        endif()
    endif()

    if("${_gentest_fmt_version}" STREQUAL "")
        message(FATAL_ERROR "gentest: failed to determine resolved fmt version for install/export metadata")
    endif()

    set(GENTEST_FMT_VERSION "${_gentest_fmt_version}" CACHE INTERNAL
        "Resolved fmt version used by gentest's install/export metadata" FORCE)
endfunction()

function(_gentest_ensure_fmt_dependency_target)
    if(NOT TARGET fmt::fmt)
        message(FATAL_ERROR "gentest: failed to locate fmt (fmt::fmt)")
    endif()

    if(NOT TARGET gentest::fmt_dependency)
        add_library(gentest::fmt_dependency INTERFACE IMPORTED GLOBAL)
        set_property(TARGET gentest::fmt_dependency PROPERTY INTERFACE_LINK_LIBRARIES "fmt::fmt")
    endif()
endfunction()

function(gentest_ensure_fmt)
    if(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # Align CRT selection with the prebuilt LLVM/Clang libraries (this
        # Windows machine's LLVM ships with /MT Release).
        if(NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
            set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded" CACHE STRING "MSVC runtime library" FORCE)
        endif()

        # Work around a clang-on-Windows constant-evaluation issue in fmt's
        # compile-time format string checks (observed with LLVM 21). Disable
        # only fmt's consteval path instead of redefining fmt's internal
        # FMT_CONSTEVAL macro (which creates warning spam).
        #
        # Note: Do not set FMT_USE_CONSTEXPR=0. fmt uses constexpr helpers in
        # headers even when building our targets as C++20, and disabling them
        # breaks compilation under clang.
        add_compile_definitions(FMT_USE_CONSTEVAL=0)

        # Prebuilt LLVM/Clang distributions typically ship release-mode STL
        # settings. Keep our build compatible when linking against those libs
        # by matching the iterator debug level across the whole build.
        add_compile_definitions(_ITERATOR_DEBUG_LEVEL=0 _HAS_ITERATOR_DEBUGGING=0)
    endif()

    if(NOT TARGET fmt::fmt)
        find_package(fmt CONFIG QUIET)

        if(NOT TARGET fmt::fmt)
            include(FetchContent)

            set(FMT_DOC OFF CACHE BOOL "Disable fmt documentation build" FORCE)
            set(FMT_TEST OFF CACHE BOOL "Disable fmt test build" FORCE)
            set(_gentest_fmt_install OFF)
            if(DEFINED gentest_INSTALL AND gentest_INSTALL)
                set(_gentest_fmt_install ON)
            endif()
            set(FMT_INSTALL ${_gentest_fmt_install} CACHE BOOL "Enable fmt install rules" FORCE)
            unset(_gentest_fmt_install)

            FetchContent_Declare(
                fmt
                GIT_REPOSITORY https://github.com/fmtlib/fmt.git
                GIT_TAG 12.1.0)
            FetchContent_MakeAvailable(fmt)

            if(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND TARGET fmt)
                # Treat fetched fmt headers as system headers for downstream
                # targets to avoid third-party warning noise in project builds.
                get_target_property(_gentest_fmt_iface_includes fmt INTERFACE_INCLUDE_DIRECTORIES)
                if(_gentest_fmt_iface_includes AND NOT _gentest_fmt_iface_includes MATCHES "-NOTFOUND$")
                    set_property(TARGET fmt APPEND PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_gentest_fmt_iface_includes}")
                endif()
                unset(_gentest_fmt_iface_includes)
                # Suppress third-party fmt warnings in its own sources.
                if(DEFINED CMAKE_CXX_COMPILER_FRONTEND_VARIANT
                   AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
                    target_compile_options(fmt PRIVATE /w)
                else()
                    target_compile_options(fmt PRIVATE -Wno-everything)
                endif()
            endif()
        endif()

        if(NOT TARGET fmt::fmt AND TARGET fmt)
            add_library(fmt::fmt ALIAS fmt)
        endif()
    endif()

    if(NOT TARGET fmt::fmt-header-only)
        if(TARGET fmt-header-only)
            add_library(fmt::fmt-header-only ALIAS fmt-header-only)
        elseif(TARGET fmt::fmt)
            set(_gentest_fmt_header_only_target gentest_fmt_header_only)
            if(TARGET "${_gentest_fmt_header_only_target}")
                message(FATAL_ERROR
                    "gentest: internal helper target '${_gentest_fmt_header_only_target}' already exists while synthesizing fmt::fmt-header-only")
            endif()
            add_library("${_gentest_fmt_header_only_target}" INTERFACE)
            add_library(fmt::fmt-header-only ALIAS "${_gentest_fmt_header_only_target}")
            get_target_property(_gentest_fmt_includes fmt::fmt INTERFACE_INCLUDE_DIRECTORIES)
            if(_gentest_fmt_includes AND NOT _gentest_fmt_includes MATCHES "-NOTFOUND$")
                target_include_directories("${_gentest_fmt_header_only_target}" INTERFACE ${_gentest_fmt_includes})
            endif()
            get_target_property(_gentest_fmt_compile_options fmt::fmt INTERFACE_COMPILE_OPTIONS)
            if(_gentest_fmt_compile_options AND NOT _gentest_fmt_compile_options MATCHES "-NOTFOUND$")
                target_compile_options("${_gentest_fmt_header_only_target}" INTERFACE ${_gentest_fmt_compile_options})
            endif()
            target_compile_definitions("${_gentest_fmt_header_only_target}" INTERFACE FMT_HEADER_ONLY=1)
            unset(_gentest_fmt_includes)
            unset(_gentest_fmt_compile_options)
            unset(_gentest_fmt_header_only_target)
        endif()
    endif()

    if(NOT TARGET fmt::fmt)
        message(FATAL_ERROR "gentest: failed to locate fmt (fmt::fmt)")
    endif()

    _gentest_ensure_fmt_dependency_target()

    if(DEFINED gentest_INSTALL AND gentest_INSTALL)
        _gentest_cache_fmt_version()
    endif()
endfunction()
