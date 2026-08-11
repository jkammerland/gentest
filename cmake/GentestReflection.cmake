include_guard(GLOBAL)

include(CheckCXXSourceCompiles)

function(gentest_check_reflection_support out_var)
    if(DEFINED GENTEST_CXX26_REFLECTION_SUPPORTED)
        set(${out_var} "${GENTEST_CXX26_REFLECTION_SUPPORTED}" PARENT_SCOPE)
        return()
    endif()

    set(_gentest_had_cxx_standard FALSE)
    set(_gentest_had_cxx_standard_required FALSE)
    set(_gentest_had_required_flags FALSE)
    if(DEFINED CMAKE_CXX_STANDARD)
        set(_gentest_saved_cxx_standard "${CMAKE_CXX_STANDARD}")
        set(_gentest_had_cxx_standard TRUE)
    endif()
    if(DEFINED CMAKE_CXX_STANDARD_REQUIRED)
        set(_gentest_saved_cxx_standard_required "${CMAKE_CXX_STANDARD_REQUIRED}")
        set(_gentest_had_cxx_standard_required TRUE)
    endif()

    set(CMAKE_CXX_STANDARD 26)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(_gentest_reflection_probe [=[
        #include <meta>

        #if !defined(__cpp_impl_reflection) || !defined(__cpp_lib_reflection)
        #error "C++26 reflection feature-test macros are unavailable"
        #endif

        namespace gentest_reflection_probe {
        [[=42]] void reflected_case() {}
        }

        template <std::meta::info Function> void invoke() { [:Function:](); }

        static_assert(std::meta::extract<int>(std::meta::annotations_of(^^gentest_reflection_probe::reflected_case).front()) == 42);
        static_assert(!std::meta::members_of(^^gentest_reflection_probe, std::meta::access_context::current()).empty());

        int main() { invoke<^^gentest_reflection_probe::reflected_case>(); }
    ]=])

    check_cxx_source_compiles("${_gentest_reflection_probe}" _gentest_reflection_without_extra_flags)
    set(_gentest_reflection_compile_option "")

    if(NOT _gentest_reflection_without_extra_flags)
        if(DEFINED CMAKE_REQUIRED_FLAGS)
            set(_gentest_saved_required_flags "${CMAKE_REQUIRED_FLAGS}")
            set(_gentest_had_required_flags TRUE)
        endif()
        string(APPEND CMAKE_REQUIRED_FLAGS " -freflection")
        check_cxx_source_compiles("${_gentest_reflection_probe}" _gentest_reflection_with_freflection)
        if(_gentest_reflection_with_freflection)
            set(_gentest_reflection_compile_option "-freflection")
        endif()
        if(_gentest_had_required_flags)
            set(CMAKE_REQUIRED_FLAGS "${_gentest_saved_required_flags}")
        else()
            unset(CMAKE_REQUIRED_FLAGS)
        endif()
    endif()

    if(_gentest_reflection_without_extra_flags OR _gentest_reflection_with_freflection)
        set(GENTEST_CXX26_REFLECTION_SUPPORTED TRUE CACHE INTERNAL
            "Whether the active C++ toolchain supports the reflection registration surface" FORCE)
    else()
        set(GENTEST_CXX26_REFLECTION_SUPPORTED FALSE CACHE INTERNAL
            "Whether the active C++ toolchain supports the reflection registration surface" FORCE)
    endif()
    set(GENTEST_CXX26_REFLECTION_COMPILE_OPTION "${_gentest_reflection_compile_option}" CACHE INTERNAL
        "Extra compile option required to enable the reflection registration surface" FORCE)

    if(_gentest_had_cxx_standard)
        set(CMAKE_CXX_STANDARD "${_gentest_saved_cxx_standard}")
    else()
        unset(CMAKE_CXX_STANDARD)
    endif()
    if(_gentest_had_cxx_standard_required)
        set(CMAKE_CXX_STANDARD_REQUIRED "${_gentest_saved_cxx_standard_required}")
    else()
        unset(CMAKE_CXX_STANDARD_REQUIRED)
    endif()

    set(${out_var} "${GENTEST_CXX26_REFLECTION_SUPPORTED}" PARENT_SCOPE)
endfunction()

function(gentest_enable_reflection target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "gentest_enable_reflection: target '${target}' does not exist")
    endif()

    gentest_check_reflection_support(_gentest_reflection_supported)
    if(NOT _gentest_reflection_supported)
        message(FATAL_ERROR
            "gentest_enable_reflection(${target}) requires a compiler and standard library that implement "
            "C++26 reflection, typed annotations, and <meta>")
    endif()

    target_compile_features(${target} PRIVATE cxx_std_26)
    if(GENTEST_CXX26_REFLECTION_COMPILE_OPTION)
        target_compile_options(${target} PRIVATE "${GENTEST_CXX26_REFLECTION_COMPILE_OPTION}")
    endif()
endfunction()
