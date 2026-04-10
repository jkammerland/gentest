# Toolchain file for cross-compiling to Linux/aarch64 with the GNU cross toolchain.
#
# Intended use:
#   cmake -S . -B build/aarch64 \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-linux-gnu.cmake \
#     -DGENTEST_CODEGEN_EXECUTABLE=/path/to/host/gentest_codegen
#
# If qemu-aarch64 is available, this toolchain also sets
# CMAKE_CROSSCOMPILING_EMULATOR so that `ctest` can run aarch64 test binaries on
# the host.

include("${CMAKE_CURRENT_LIST_DIR}/../GentestGnuQemuToolchain.cmake")

gentest_configure_gnu_qemu_cross_toolchain(
    SYSTEM_PROCESSOR aarch64
    TARGET_TRIPLE aarch64-linux-gnu
    LOADER_NAME ld-linux-aarch64.so.1
    QEMU_NAMES
        qemu-aarch64
        qemu-aarch64-static
    LOADER_DIR_SUFFIXES
        lib
        lib64
        usr/lib
        usr/lib64
    CANDIDATE_ROOTS
        /usr/aarch64-linux-gnu
        /usr/aarch64-linux-gnu/sys-root)
