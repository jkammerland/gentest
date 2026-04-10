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

include("${CMAKE_CURRENT_LIST_DIR}/../GentestGnuQemuToolchain.cmake")

gentest_configure_gnu_qemu_cross_toolchain(
    SYSTEM_PROCESSOR riscv64
    TARGET_TRIPLE riscv64-linux-gnu
    LOADER_NAME ld-linux-riscv64-lp64d.so.1
    QEMU_NAMES
        qemu-riscv64
        qemu-riscv64-static
    LOADER_DIR_SUFFIXES
        lib
        lib64
        lib/lp64d
        lib64/lp64d
        usr/lib
        usr/lib64
        usr/lib/lp64d
        usr/lib64/lp64d
    CANDIDATE_ROOTS
        /usr/riscv64-linux-gnu
        /usr/riscv64-linux-gnu/sys-root)
