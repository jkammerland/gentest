# Install / Use on Linux

>[!NOTE]
> This is a template document. Keep it lightweight and add distro-specific commands as needed.

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

For working Linux cross-compilation examples with QEMU user-mode test execution, see:

- [`docs/cross_compile/linux_aarch64_qemu.md`](../cross_compile/linux_aarch64_qemu.md)
- [`docs/cross_compile/linux_riscv64_qemu.md`](../cross_compile/linux_riscv64_qemu.md)
- `scripts/poc_cross_aarch64_qemu.sh`
- `scripts/poc_cross_riscv64_qemu.sh`
- CMake presets: `cmake --workflow --preset=host-codegen`, then `cmake --preset=aarch64-qemu`, `cmake --build --preset=aarch64-qemu`, and `ctest --preset=aarch64-qemu`
- CMake presets: `cmake --workflow --preset=host-codegen`, then `cmake --preset=riscv64-qemu`, `cmake --build --preset=riscv64-qemu`, and `ctest --preset=riscv64-qemu`
