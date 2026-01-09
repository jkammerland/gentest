# Windows build + codegen troubleshooting

>[!NOTE]
> This is a note for LLMs

>[!NOTE]
> This page is for current `master`. A historical investigation write-up is archived at
> `docs/archive/windows_troubleshooting_2026-01-08.md`.

`gentest` builds a host tool (`gentest_codegen`) during the build and runs it to generate test sources. On Windows, most
issues come from LLVM/Clang discovery or the chosen CMake generator.

## Quick checklist

- Prefer Ninja (`-G Ninja`) over Visual Studio generators for local iteration.
- Use a recent LLVM/Clang distribution and put its `bin` on `PATH`.
- Pass `-DLLVM_DIR=...` and `-DClang_DIR=...` when configuring (or set them in the environment).
- Ensure `compile_commands.json` is produced (this repo sets `CMAKE_EXPORT_COMPILE_COMMANDS=ON` by default).

## Build this repo (Clang + Ninja)

From `B:\\repos\\gentest` in PowerShell:

```pwsh
$llvm = 'C:\Tools\llvm-21.1.4'
$env:LLVM_BIN = "$llvm\bin"; $env:PATH = "$env:LLVM_BIN;$env:PATH"
$env:LLVM_DIR = "$llvm\lib\cmake\llvm"; $env:Clang_DIR = "$llvm\lib\cmake\clang"

cmake --preset=debug-system `
  -G Ninja `
  -DCMAKE_C_COMPILER="$env:LLVM_BIN\clang.exe" `
  -DCMAKE_CXX_COMPILER="$env:LLVM_BIN\clang++.exe" `
  -DLLVM_DIR="$env:LLVM_DIR" `
  -DClang_DIR="$env:Clang_DIR"
cmake --build --preset=debug-system
ctest --preset=debug-system --output-on-failure
```

## Build this repo (MSVC + LLVM tooling, Ninja)

From `B:\\repos\\gentest` in PowerShell (Developer prompt env required):

```pwsh
& "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat" -arch=amd64

$msvcBuildDir = 'build\debug-system-msvc'
cmake -S . -B $msvcBuildDir -G Ninja -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl `
  -DLLVM_DIR="$env:LLVM_DIR" -DClang_DIR="$env:Clang_DIR"
cmake --build $msvcBuildDir
ctest --test-dir $msvcBuildDir --output-on-failure
```

## Common issues

### CMake cannot find LLVM/Clang

- `LLVM_DIR` should point at `...\\lib\\cmake\\llvm`, and `Clang_DIR` at `...\\lib\\cmake\\clang`.
- Ensure you are not mixing mismatched LLVM/Clang installs.

### `gentest_codegen` fails parsing with missing builtin headers

If clang cannot find its builtin headers, check the resource dir:

```pwsh
& "$env:LLVM_BIN\clang.exe" -print-resource-dir
```

If needed, pass `-resource-dir <path>` to the code generator via `gentest_attach_codegen(... CLANG_ARGS ...)` in your
project.

### LLVM 20 `diaguids.lib` path issues

Some LLVM 20 Windows distributions reference a `diaguids.lib` path that may not exist on the current machine. If link
errors mention `diaguids.lib`, install a Visual Studio DIA SDK (or use an LLVM/Clang distribution that does not embed
this dependency).
