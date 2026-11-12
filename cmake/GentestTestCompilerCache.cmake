include_guard(GLOBAL)

# Fresh fixture builds must still create their own targets and execute their
# build graph. A compiler cache can reuse identical object files underneath it.
function(gentest_enable_test_compiler_cache)
    if(NOT CMAKE_HOST_UNIX OR NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$"
       OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        message(FATAL_ERROR "GENTEST_TEST_COMPILER_CACHE requires a GNU-style C++ compiler on a Unix host")
    endif()
    find_program(GENTEST_CCACHE_EXECUTABLE NAMES ccache REQUIRED)
    if(NOT EXISTS "${GENTEST_CCACHE_EXECUTABLE}" OR IS_DIRECTORY "${GENTEST_CCACHE_EXECUTABLE}")
        message(FATAL_ERROR "GENTEST_CCACHE_EXECUTABLE must name a compiler-cache executable")
    endif()
    foreach(_language IN ITEMS C CXX)
        if(CMAKE_${_language}_COMPILER_LAUNCHER
           AND NOT CMAKE_${_language}_COMPILER_LAUNCHER STREQUAL "ccache"
           AND NOT CMAKE_${_language}_COMPILER_LAUNCHER STREQUAL GENTEST_CCACHE_EXECUTABLE)
            message(FATAL_ERROR "GENTEST_TEST_COMPILER_CACHE cannot replace CMAKE_${_language}_COMPILER_LAUNCHER")
        endif()
        set(CMAKE_${_language}_COMPILER_LAUNCHER "${GENTEST_CCACHE_EXECUTABLE}" PARENT_SCOPE)
    endforeach()

    # Normalize the compilation directory in debug info, without changing
    # __FILE__ or source_location. CMake exports private module compile options
    # too, so keep this build-directory mapping out of every exported target.
    # Keep ccache's compiler/header validation.
    add_compile_options(
        "$<BUILD_LOCAL_INTERFACE:$<$<COMPILE_LANG_AND_ID:C,GNU,Clang,AppleClang>:-fdebug-prefix-map=${CMAKE_BINARY_DIR}=.>>"
        "$<BUILD_LOCAL_INTERFACE:$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-fdebug-prefix-map=${CMAKE_BINARY_DIR}=.>>")
    message(STATUS "gentest: compiler cache launcher '${GENTEST_CCACHE_EXECUTABLE}'")
endfunction()

# An explicit -D selection must also reach independently configured fixtures.
function(gentest_configure_test_compiler_cache test_name)
    if(DEFINED GENTEST_TEST_COMPILER_CACHE)
        set_property(TEST "${test_name}" APPEND PROPERTY ENVIRONMENT
            "GENTEST_TEST_COMPILER_CACHE=${GENTEST_TEST_COMPILER_CACHE}")
    endif()
endfunction()
