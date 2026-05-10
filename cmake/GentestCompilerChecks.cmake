include_guard(GLOBAL)

include(CheckCXXSourceCompiles)

function(gentest_require_std_stop_token)
    set(_gentest_prev_try_compile_target_type "${CMAKE_TRY_COMPILE_TARGET_TYPE}")
    if(DEFINED CMAKE_CXX_STANDARD)
        set(_gentest_prev_cxx_standard "${CMAKE_CXX_STANDARD}")
    endif()
    if(DEFINED CMAKE_CXX_STANDARD_REQUIRED)
        set(_gentest_prev_cxx_standard_required "${CMAKE_CXX_STANDARD_REQUIRED}")
    endif()
    if(DEFINED CMAKE_CXX_EXTENSIONS)
        set(_gentest_prev_cxx_extensions "${CMAKE_CXX_EXTENSIONS}")
    endif()
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)

    check_cxx_source_compiles([=[
#include <stop_token>

int main() {
    std::stop_source source;
    std::stop_token token = source.get_token();
    source.request_stop();
    return token.stop_requested() ? 0 : 1;
}
]=] GENTEST_HAS_STD_STOP_TOKEN)

    if(DEFINED _gentest_prev_try_compile_target_type AND NOT "${_gentest_prev_try_compile_target_type}" STREQUAL "")
        set(CMAKE_TRY_COMPILE_TARGET_TYPE "${_gentest_prev_try_compile_target_type}")
    else()
        unset(CMAKE_TRY_COMPILE_TARGET_TYPE)
    endif()
    if(DEFINED _gentest_prev_cxx_standard)
        set(CMAKE_CXX_STANDARD "${_gentest_prev_cxx_standard}")
    else()
        unset(CMAKE_CXX_STANDARD)
    endif()
    if(DEFINED _gentest_prev_cxx_standard_required)
        set(CMAKE_CXX_STANDARD_REQUIRED "${_gentest_prev_cxx_standard_required}")
    else()
        unset(CMAKE_CXX_STANDARD_REQUIRED)
    endif()
    if(DEFINED _gentest_prev_cxx_extensions)
        set(CMAKE_CXX_EXTENSIONS "${_gentest_prev_cxx_extensions}")
    else()
        unset(CMAKE_CXX_EXTENSIONS)
    endif()

    if(NOT GENTEST_HAS_STD_STOP_TOKEN)
        if(APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
            message(FATAL_ERROR
                "gentest requires C++20 <stop_token>, but this AppleClang/libc++ toolchain does not provide std::stop_token. "
                "AppleClang support starts at Xcode 26 / AppleClang 17. Use Xcode 26+ or Homebrew LLVM.")
        endif()
        message(FATAL_ERROR
            "gentest requires C++20 <stop_token>, but the active C++ standard library does not provide std::stop_token.")
    endif()
endfunction()
