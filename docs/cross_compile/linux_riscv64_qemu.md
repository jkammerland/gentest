# Cross-compile (Linux → riscv64) + run under QEMU

This is a proof-of-concept workflow for cross-compiling `gentest`’s test executables to Linux/riscv64 and running them on an x86_64 Linux host with `qemu-riscv64`.

## Prerequisites

- CMake ≥ 3.31
- Ninja
- GNU cross toolchain: `riscv64-linux-gnu-gcc` / `riscv64-linux-gnu-g++`
- A riscv64 glibc/sysroot (Ubuntu/Debian cross packages are sufficient in CI)
- QEMU user-mode: `qemu-riscv64`
- A host LLVM/Clang toolchain capable of building `gentest_codegen`

## One-command POC (recommended)

Run:

```bash
./scripts/poc_cross_riscv64_qemu.sh
```

This:

1) builds a host `gentest_codegen`
2) cross-compiles `gentest_unit_tests` and `gentest_mocking_tests` using `cmake/toolchains/riscv64-linux-gnu.cmake`
3) runs a small `ctest` subset (unit suite + mocking suite + a codegen check)

Run the full test set (can be slow under emulation):

```bash
./scripts/poc_cross_riscv64_qemu.sh --full
```

### Custom local toolchain / sysroot

If your distro ships only kernel-oriented riscv cross packages, point the script at a complete userspace toolchain and sysroot:

```bash
TARGET_TRIPLE=riscv64-unknown-linux-gnu \
TARGET_CC="$HOME/.local/toolchains/riscv-gnu-toolchain-2026.01.23/riscv/bin/riscv64-unknown-linux-gnu-gcc" \
TARGET_CXX="$HOME/.local/toolchains/riscv-gnu-toolchain-2026.01.23/riscv/bin/riscv64-unknown-linux-gnu-g++" \
SYSROOT="$HOME/.local/toolchains/riscv-gnu-toolchain-2026.01.23/riscv/sysroot" \
./scripts/poc_cross_riscv64_qemu.sh
```

## Manual workflow

### 1) Build the host code generator

```bash
cmake -S . -B build/host-codegen -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -Dgentest_BUILD_TESTING=OFF \
  -DGENTEST_BUILD_CODEGEN=ON
cmake --build build/host-codegen --target gentest_codegen
```

### 2) Configure the riscv64 build

```bash
cmake -S . -B build/riscv64-qemu -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/riscv64-linux-gnu.cmake \
  -DGENTEST_BUILD_CODEGEN=OFF \
  -DGENTEST_CODEGEN_EXECUTABLE="$PWD/build/host-codegen/tools/gentest_codegen" \
  -DGENTEST_ENABLE_PACKAGE_TESTS=OFF
```

When using a non-default toolchain/sysroot, also pass the target compiler and sysroot explicitly:

```bash
cmake -S . -B build/riscv64-qemu -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/riscv64-linux-gnu.cmake \
  -DCMAKE_C_COMPILER="$HOME/.local/toolchains/riscv-gnu-toolchain-2026.01.23/riscv/bin/riscv64-unknown-linux-gnu-gcc" \
  -DCMAKE_CXX_COMPILER="$HOME/.local/toolchains/riscv-gnu-toolchain-2026.01.23/riscv/bin/riscv64-unknown-linux-gnu-g++" \
  -DCMAKE_C_COMPILER_TARGET=riscv64-unknown-linux-gnu \
  -DCMAKE_CXX_COMPILER_TARGET=riscv64-unknown-linux-gnu \
  -DCMAKE_SYSROOT="$HOME/.local/toolchains/riscv-gnu-toolchain-2026.01.23/riscv/sysroot" \
  -DGENTEST_BUILD_CODEGEN=OFF \
  -DGENTEST_CODEGEN_EXECUTABLE="$PWD/build/host-codegen/tools/gentest_codegen" \
  -DGENTEST_ENABLE_PACKAGE_TESTS=OFF
```

### 3) Build + run (via CTest)

```bash
cmake --build build/riscv64-qemu --target gentest_unit_tests gentest_mocking_tests
ctest --test-dir build/riscv64-qemu --output-on-failure \
  -R '^(gentest_codegen_check_valid|unit|unit_inventory|mocking)$'
```

## CMake presets

If you prefer presets over the script (defaults assume the host tool is at `build/host-codegen/tools/gentest_codegen`):

```bash
cmake --workflow --preset=host-codegen
cmake --preset=riscv64-qemu
cmake --build --preset=riscv64-qemu
ctest --preset=riscv64-qemu --output-on-failure

cmake --workflow --preset=host-codegen
cmake --preset=riscv64-qemu
cmake --build --preset=riscv64-qemu-full
ctest --preset=riscv64-qemu-full --output-on-failure   # full suite (slow)
```

## Invoke from other build systems

- xmake: `xmake run poc_cross_riscv64_qemu`
- Meson: `meson compile -C build/meson poc_cross_riscv64_qemu`
- Bazel: `bazel run //:poc_cross_riscv64_qemu`

## Notes

- The toolchain file tries to locate a riscv64 runtime root from the target compiler, then sets `CMAKE_CROSSCOMPILING_EMULATOR` to `qemu-riscv64 -L <root>` when available.
- To use a custom sysroot/runtime root, configure with `-DCMAKE_SYSROOT=/path/to/sysroot` and/or set `-DCMAKE_CROSSCOMPILING_EMULATOR="qemu-riscv64;-L;/path/to/root"`.
- On distros where `riscv64-linux-gnu-gcc` exists but the userspace sysroot is absent or incomplete, use a full userspace toolchain/sysroot and point the configure step at it explicitly.
