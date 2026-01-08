# Install / Use on macOS

>[!NOTE]
> This is a template document. Keep it lightweight and add Homebrew/Xcode specifics as needed.

## Prerequisites

- CMake ≥ 3.31
- A C++20 compiler (runtime); this repo builds/tests its executables as C++23 by default (see `GENTEST_TEST_CXX_STANDARD`)
- LLVM/Clang libraries (required to build/run `gentest_codegen`)

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
