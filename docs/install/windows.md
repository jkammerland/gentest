# Install / Use on Windows

>[!NOTE]
> This is a template document. Keep it lightweight and add MSVC/clang-cl specifics as needed.

## Prerequisites

- CMake ≥ 3.31
- Ninja (recommended) or Visual Studio generator
- A C++20 toolchain (runtime); this repo builds/tests its executables as C++23 by default (see `GENTEST_TEST_CXX_STANDARD`)
- LLVM/Clang tooling (required to build/run `gentest_codegen`)

>[!IMPORTANT]
> `gentest_codegen` consumes your build’s `compile_commands.json`. Keep `CMAKE_EXPORT_COMPILE_COMMANDS=ON`.

## Configure LLVM/Clang (required)

`debug-system` expects a system LLVM/Clang install. In PowerShell, set `LLVM_DIR` and `Clang_DIR` to match your LLVM
distribution:

```pwsh
$llvm = 'C:\Tools\llvm-21.1.4'
$env:LLVM_BIN = "$llvm\bin"; $env:PATH = "$env:LLVM_BIN;$env:PATH"
$env:LLVM_DIR = "$llvm\lib\cmake\llvm"; $env:Clang_DIR = "$llvm\lib\cmake\clang"
```

## Build this repo (recommended)

```pwsh
cmake --preset=debug-system -G Ninja `
  -DCMAKE_C_COMPILER="$env:LLVM_BIN\clang.exe" `
  -DCMAKE_CXX_COMPILER="$env:LLVM_BIN\clang++.exe" `
  -DLLVM_DIR="$env:LLVM_DIR" `
  -DClang_DIR="$env:Clang_DIR"
cmake --build --preset=debug-system
ctest --preset=debug-system --output-on-failure
```

## Alternative: MSVC + LLVM tooling

```pwsh
& "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat" -arch=amd64

$msvcBuildDir = 'build\debug-system-msvc'
cmake -S . -B $msvcBuildDir -G Ninja -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl `
  -DLLVM_DIR="$env:LLVM_DIR" -DClang_DIR="$env:Clang_DIR"
cmake --build $msvcBuildDir
ctest --test-dir $msvcBuildDir --output-on-failure
```

## Troubleshooting

- See [`docs/windows_troubleshooting.md`](../windows_troubleshooting.md) for common LLVM/Clang and environment issues.
