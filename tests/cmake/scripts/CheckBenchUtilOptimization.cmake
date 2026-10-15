include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

set(_work "${BUILD_ROOT}/bench_util_optimization")
file(MAKE_DIRECTORY "${_work}")
set(_assembly "${_work}/optimization.s")

# Use the configured compiler and target, but explicitly optimize this probe
# even in Debug builds. The production helper's LTO boundary is tested natively.
set(_extra)
gentest_append_host_apple_sysroot_compile_args(_extra)
gentest_is_msvc_style_compiler(_msvc "${CXX_COMPILER}")
if(CXX_COMPILER_TARGET AND NOT _msvc)
    list(APPEND _extra "--target=${CXX_COMPILER_TARGET}")
endif()
if(CXX_SYSROOT AND NOT _msvc)
    list(APPEND _extra "--sysroot=${CXX_SYSROOT}")
endif()
if(CXX_FLAGS)
    separate_arguments(_configured_flags NATIVE_COMMAND "${CXX_FLAGS}")
    list(APPEND _extra ${_configured_flags})
endif()

if(_msvc)
    list(APPEND _extra /O2 /Ob1 /FA "/Fa${_assembly}")
    if(CXX_COMPILER_ID STREQUAL "MSVC")
        list(APPEND _extra /GL-)
    else()
        list(APPEND _extra /clang:-fno-lto)
    endif()
    set(_object "${_work}/optimization.obj")
else()
    list(APPEND _extra -O2 -fno-lto)
    set(_object "${_assembly}")
endif()

gentest_make_compile_only_command_args(_command
    COMPILER "${CXX_COMPILER}"
    COMPILER_ARG1 "${CXX_COMPILER_ARG1}"
    STD "-std=c++20"
    SOURCE "${SOURCE_DIR}/tests/bench_util/optimization.cpp"
    OBJECT "${_object}"
    INCLUDE_ARGS "-I${SOURCE_DIR}/include"
    EXTRA_ARGS ${_extra})
if(NOT _msvc)
    list(APPEND _command -S)
endif()

execute_process(COMMAND ${_command} RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "Optimized benchmark helper probe failed to compile:\n${_out}\n${_err}")
endif()

execute_process(COMMAND "${PYTHON}" "${SOURCE_DIR}/tests/scripts/check_bench_util_assembly.py" "${_assembly}"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "Optimized benchmark helper contract failed:\n${_out}\n${_err}")
endif()
message(STATUS "${_out}")
