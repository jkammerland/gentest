include_guard(GLOBAL)

function(gentest_ensure_fmt)
    if(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # Align CRT selection with the prebuilt LLVM/Clang libraries (this
        # Windows machine's LLVM ships with /MT Release).
        if(NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
            set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded" CACHE STRING "MSVC runtime library" FORCE)
        endif()

        # Work around a clang-on-Windows constant-evaluation issue in fmt's
        # compile-time format string checks (observed with LLVM 21, and likely
        # affects LLVM 20 as well).
        add_compile_definitions(FMT_USE_CONSTEXPR=0 FMT_CONSTEVAL=)

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
                GIT_TAG 10.2.1)
            FetchContent_MakeAvailable(fmt)
        endif()

        if(NOT TARGET fmt::fmt AND TARGET fmt)
            add_library(fmt::fmt ALIAS fmt)
        endif()
    endif()

    if(NOT TARGET fmt::fmt-header-only)
        if(TARGET fmt-header-only)
            add_library(fmt::fmt-header-only ALIAS fmt-header-only)
        elseif(TARGET fmt::fmt)
            add_library(fmt::fmt-header-only INTERFACE)
            get_target_property(_gentest_fmt_includes fmt::fmt INTERFACE_INCLUDE_DIRECTORIES)
            if(_gentest_fmt_includes AND NOT _gentest_fmt_includes MATCHES "-NOTFOUND$")
                target_include_directories(fmt::fmt-header-only INTERFACE ${_gentest_fmt_includes})
            endif()
            target_compile_definitions(fmt::fmt-header-only INTERFACE FMT_HEADER_ONLY=1)
            unset(_gentest_fmt_includes)
        endif()
    endif()

    if(NOT TARGET fmt::fmt)
        message(FATAL_ERROR "gentest: failed to locate fmt (fmt::fmt)")
    endif()
endfunction()
