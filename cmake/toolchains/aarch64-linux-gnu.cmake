# Toolchain file for cross-compiling to Linux/aarch64 with the GNU cross toolchain.
#
# Intended use:
#   cmake -S . -B build/aarch64 \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-linux-gnu.cmake \
#     -DGENTEST_CODEGEN_EXECUTABLE=/path/to/host/gentest_codegen
#
# If qemu-aarch64 is available, this toolchain also sets CMAKE_CROSSCOMPILING_EMULATOR
# so that `ctest` can run aarch64 test binaries on the host.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

if(NOT DEFINED CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
endif()

if(NOT DEFINED CMAKE_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
endif()

if(NOT DEFINED CMAKE_C_COMPILER_TARGET)
    set(CMAKE_C_COMPILER_TARGET aarch64-linux-gnu)
endif()

if(NOT DEFINED CMAKE_CXX_COMPILER_TARGET)
    set(CMAKE_CXX_COMPILER_TARGET aarch64-linux-gnu)
endif()

set(_gentest_qemu_sysroot "")
execute_process(
    COMMAND ${CMAKE_C_COMPILER} -print-file-name=ld-linux-aarch64.so.1
    OUTPUT_VARIABLE _gentest_loader
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(_gentest_loader AND NOT _gentest_loader STREQUAL "ld-linux-aarch64.so.1" AND IS_ABSOLUTE "${_gentest_loader}" AND EXISTS "${_gentest_loader}")
    get_filename_component(_gentest_loader_dir "${_gentest_loader}" DIRECTORY)
    get_filename_component(_gentest_qemu_sysroot "${_gentest_loader_dir}" DIRECTORY)
    get_filename_component(_gentest_qemu_sysroot "${_gentest_qemu_sysroot}" REALPATH)
    unset(_gentest_loader_dir)
endif()
unset(_gentest_loader)

if(NOT DEFINED CMAKE_SYSROOT OR CMAKE_SYSROOT STREQUAL "")
    execute_process(
        COMMAND ${CMAKE_C_COMPILER} -print-sysroot
        OUTPUT_VARIABLE _gentest_sysroot_from_compiler
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)

    # Some distros report '/' (or an empty string) as the compiler sysroot even
    # though the target runtime lives under a multiarch prefix like
    # /usr/aarch64-linux-gnu. Avoid forcing CMAKE_SYSROOT to such prefixes since
    # it can break the compiler's built-in search paths.
    if(_gentest_sysroot_from_compiler AND EXISTS "${_gentest_sysroot_from_compiler}" AND NOT _gentest_sysroot_from_compiler STREQUAL "/")
        get_filename_component(_gentest_sysroot_from_compiler "${_gentest_sysroot_from_compiler}" REALPATH)
        set(CMAKE_SYSROOT "${_gentest_sysroot_from_compiler}")
    endif()
    unset(_gentest_sysroot_from_compiler)
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
    find_program(_gentest_qemu_aarch64 NAMES qemu-aarch64 qemu-aarch64-static)
    if(_gentest_qemu_aarch64 AND _gentest_qemu_sysroot AND EXISTS "${_gentest_qemu_sysroot}")
        set(CMAKE_CROSSCOMPILING_EMULATOR "${_gentest_qemu_aarch64};-L;${_gentest_qemu_sysroot}")
    elseif(_gentest_qemu_aarch64 AND DEFINED CMAKE_SYSROOT AND NOT CMAKE_SYSROOT STREQUAL "")
        set(CMAKE_CROSSCOMPILING_EMULATOR "${_gentest_qemu_aarch64};-L;${CMAKE_SYSROOT}")
    endif()
endif()

unset(_gentest_qemu_aarch64)
unset(_gentest_qemu_sysroot)
