# Cross-compile (Linux → aarch64) + run under QEMU

This is a proof-of-concept workflow for cross-compiling `gentest`’s test executables to Linux/aarch64 and running them on an x86_64 Linux host with `qemu-aarch64`.

## Prerequisites

- CMake ≥ 3.31
- Ninja
- GNU cross toolchain: `aarch64-linux-gnu-gcc` / `aarch64-linux-gnu-g++`
- QEMU user-mode: `qemu-aarch64`
- A host LLVM/Clang toolchain capable of building `gentest_codegen`

## One-command POC (recommended)

Run:

```bash
./scripts/poc_cross_aarch64_qemu.sh
```

This:

1) builds a host `gentest_codegen`
2) cross-compiles `gentest_unit_tests` using `cmake/toolchains/aarch64-linux-gnu.cmake`
3) runs a small `ctest` subset (unit suite + a codegen check)

Run the full test set (can be slow under emulation):

```bash
./scripts/poc_cross_aarch64_qemu.sh --full
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

### 2) Configure the aarch64 build

```bash
cmake -S . -B build/aarch64-qemu -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-linux-gnu.cmake \
  -DGENTEST_BUILD_CODEGEN=OFF \
  -DGENTEST_CODEGEN_EXECUTABLE="$PWD/build/host-codegen/tools/gentest_codegen" \
  -DGENTEST_ENABLE_PACKAGE_TESTS=OFF
```

### 3) Build + run (via CTest)

```bash
cmake --build build/aarch64-qemu --target gentest_unit_tests
ctest --test-dir build/aarch64-qemu --output-on-failure \
  -R '^(gentest_codegen_check_valid|unit|unit_counts|unit_list_counts)$'
```

## CMake presets

If you prefer presets over the script:

```bash
cmake --workflow --preset=host-codegen
cmake --workflow --preset=aarch64-qemu
```

## Invoke from other build systems

- xmake: `xmake run poc_cross_aarch64_qemu`
- Meson: `meson compile -C build/meson poc_cross_aarch64_qemu`
- Bazel: `bazel run //:poc_cross_aarch64_qemu`

## Notes

- The toolchain file auto-detects the cross sysroot via `aarch64-linux-gnu-gcc -print-sysroot` and sets `CMAKE_CROSSCOMPILING_EMULATOR` to `qemu-aarch64 -L <sysroot>` when available.
- If you need a custom sysroot, set `-DCMAKE_SYSROOT=/path/to/sysroot` when configuring the target build.
