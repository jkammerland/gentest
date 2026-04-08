# Toolchain file for cross-compiling to Linux/riscv64 with the GNU cross toolchain.
#
# Intended use:
#   cmake -S . -B build/riscv64-qemu \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/riscv64-linux-gnu.cmake \
#     -DGENTEST_CODEGEN_EXECUTABLE=/path/to/host/gentest_codegen
#
# If qemu-riscv64 is available, this toolchain also sets
# CMAKE_CROSSCOMPILING_EMULATOR so that `ctest` can run riscv64 test binaries on
# the host.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR riscv64)

if(NOT DEFINED CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER riscv64-linux-gnu-gcc)
endif()

if(NOT DEFINED CMAKE_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER riscv64-linux-gnu-g++)
endif()

if(NOT DEFINED CMAKE_C_COMPILER_TARGET)
    set(CMAKE_C_COMPILER_TARGET riscv64-linux-gnu)
endif()

if(NOT DEFINED CMAKE_CXX_COMPILER_TARGET)
    set(CMAKE_CXX_COMPILER_TARGET riscv64-linux-gnu)
endif()

set(_gentest_riscv64_loader_name "ld-linux-riscv64-lp64d.so.1")

function(_gentest_riscv64_set_runtime_root_from_loader loader_path out_var)
    set(_root "")
    if(loader_path
       AND NOT loader_path STREQUAL "${_gentest_riscv64_loader_name}"
       AND IS_ABSOLUTE "${loader_path}"
       AND EXISTS "${loader_path}")
        get_filename_component(_loader_dir "${loader_path}" DIRECTORY)
        get_filename_component(_root "${_loader_dir}" DIRECTORY)
        get_filename_component(_root "${_root}" REALPATH)
        unset(_loader_dir)
    endif()
    set(${out_var} "${_root}" PARENT_SCOPE)
endfunction()

function(_gentest_riscv64_root_has_loader root_path out_var)
    set(_has_loader FALSE)
    if(root_path AND IS_DIRECTORY "${root_path}")
        foreach(_candidate IN ITEMS
                "${root_path}/lib/${_gentest_riscv64_loader_name}"
                "${root_path}/lib64/${_gentest_riscv64_loader_name}"
                "${root_path}/lib/lp64d/${_gentest_riscv64_loader_name}"
                "${root_path}/lib64/lp64d/${_gentest_riscv64_loader_name}"
                "${root_path}/usr/lib/${_gentest_riscv64_loader_name}"
                "${root_path}/usr/lib64/${_gentest_riscv64_loader_name}"
                "${root_path}/usr/lib/lp64d/${_gentest_riscv64_loader_name}"
                "${root_path}/usr/lib64/lp64d/${_gentest_riscv64_loader_name}")
            if(EXISTS "${_candidate}")
                set(_has_loader TRUE)
                break()
            endif()
        endforeach()
    endif()
    set(${out_var} ${_has_loader} PARENT_SCOPE)
endfunction()

function(_gentest_riscv64_root_has_headers root_path out_var)
    if(root_path AND EXISTS "${root_path}/usr/include/stdio.h")
        set(${out_var} TRUE PARENT_SCOPE)
    else()
        set(${out_var} FALSE PARENT_SCOPE)
    endif()
endfunction()

set(_gentest_qemu_sysroot "")
execute_process(
    COMMAND ${CMAKE_C_COMPILER} -print-file-name=${_gentest_riscv64_loader_name}
    OUTPUT_VARIABLE _gentest_loader
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
_gentest_riscv64_set_runtime_root_from_loader("${_gentest_loader}" _gentest_qemu_sysroot)
unset(_gentest_loader)

if(NOT DEFINED CMAKE_SYSROOT OR CMAKE_SYSROOT STREQUAL "")
    execute_process(
        COMMAND ${CMAKE_C_COMPILER} -print-sysroot
        OUTPUT_VARIABLE _gentest_sysroot_from_compiler
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)

    if(_gentest_sysroot_from_compiler
       AND EXISTS "${_gentest_sysroot_from_compiler}"
       AND NOT _gentest_sysroot_from_compiler STREQUAL "/")
        get_filename_component(_gentest_sysroot_from_compiler "${_gentest_sysroot_from_compiler}" REALPATH)
        _gentest_riscv64_root_has_headers("${_gentest_sysroot_from_compiler}" _gentest_sysroot_has_headers)
        if(_gentest_sysroot_has_headers)
            set(CMAKE_SYSROOT "${_gentest_sysroot_from_compiler}")
        endif()

        if(NOT _gentest_qemu_sysroot)
            _gentest_riscv64_root_has_loader("${_gentest_sysroot_from_compiler}" _gentest_sysroot_has_loader)
            if(_gentest_sysroot_has_loader)
                set(_gentest_qemu_sysroot "${_gentest_sysroot_from_compiler}")
            endif()
            unset(_gentest_sysroot_has_loader)
        endif()

        unset(_gentest_sysroot_has_headers)
    endif()
    unset(_gentest_sysroot_from_compiler)
endif()

if(NOT _gentest_qemu_sysroot)
    foreach(_gentest_candidate_root IN ITEMS
            "/usr/${CMAKE_C_COMPILER_TARGET}"
            "/usr/riscv64-linux-gnu"
            "/usr/riscv64-linux-gnu/sys-root")
        _gentest_riscv64_root_has_loader("${_gentest_candidate_root}" _gentest_candidate_has_loader)
        if(_gentest_candidate_has_loader)
            get_filename_component(_gentest_qemu_sysroot "${_gentest_candidate_root}" REALPATH)
            break()
        endif()
        unset(_gentest_candidate_has_loader)
    endforeach()
    unset(_gentest_candidate_root)
endif()

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
    find_program(_gentest_qemu_riscv64 NAMES qemu-riscv64 qemu-riscv64-static)
    if(_gentest_qemu_riscv64 AND _gentest_qemu_sysroot AND EXISTS "${_gentest_qemu_sysroot}")
        set(CMAKE_CROSSCOMPILING_EMULATOR "${_gentest_qemu_riscv64};-L;${_gentest_qemu_sysroot}")
    elseif(_gentest_qemu_riscv64 AND DEFINED CMAKE_SYSROOT AND NOT CMAKE_SYSROOT STREQUAL "")
        set(CMAKE_CROSSCOMPILING_EMULATOR "${_gentest_qemu_riscv64};-L;${CMAKE_SYSROOT}")
    endif()
endif()

unset(_gentest_qemu_riscv64)
unset(_gentest_qemu_sysroot)
unset(_gentest_riscv64_loader_name)
