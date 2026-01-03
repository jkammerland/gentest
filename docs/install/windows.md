# Install / Use on Windows

>[!NOTE]
> This is a template document. Keep it lightweight and add MSVC/clang-cl specifics as needed.

## Prerequisites

- CMake ≥ 3.31
- Ninja (recommended) or Visual Studio generator
- A C++23 toolchain (MSVC or clang-cl)
- LLVM/Clang tooling (required to build/run `gentest_codegen`)

>[!IMPORTANT]
> `gentest_codegen` consumes your build’s `compile_commands.json`. Keep `CMAKE_EXPORT_COMPILE_COMMANDS=ON`.

## Build this repo (recommended)

```pwsh
cmake --preset=debug-system
cmake --build --preset=debug-system
ctest --preset=debug-system --output-on-failure
```

## Troubleshooting

- See `docs/windows_troubleshooting.md` for common LLVM/Clang and environment issues.

