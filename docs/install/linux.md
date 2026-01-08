# Install / Use on Linux

>[!NOTE]
> This is a template document. Keep it lightweight and add distro-specific commands as needed.

## Prerequisites

- CMake ≥ 3.31
- A C++20 compiler (runtime); this repo builds/tests its executables as C++23 by default (see `GENTEST_TEST_CXX_STANDARD`)
- LLVM/Clang (required to build/run `gentest_codegen`)

>[!IMPORTANT]
> `gentest_codegen` consumes your build’s `compile_commands.json`. Keep `CMAKE_EXPORT_COMPILE_COMMANDS=ON`.

## Build this repo (recommended)

```bash
cmake --preset=debug-system
cmake --build --preset=debug-system
ctest --preset=debug-system --output-on-failure
```

## Use in your project

- Ensure your build produces `compile_commands.json`.
- Ensure `gentest_codegen` is runnable at build time (it runs on the host during the build).

## Cross-compiling

>[!WARNING]
> When `CMAKE_CROSSCOMPILING=TRUE`, you still need a *host* `gentest_codegen` executable. Build it separately and set `GENTEST_CODEGEN_EXECUTABLE` in the target build.

For a working Linux→aarch64 example (including `qemu-aarch64` test execution), see:

- [`docs/cross_compile/linux_aarch64_qemu.md`](../cross_compile/linux_aarch64_qemu.md)
- `scripts/poc_cross_aarch64_qemu.sh`
- CMake presets: `cmake --workflow --preset=host-codegen` then `cmake --workflow --preset=aarch64-qemu`
