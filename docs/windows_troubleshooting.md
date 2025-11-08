# Windows Build + Codegen Troubleshooting (Status + Fixes)

Branch Context
- Branch: windows-ci-msvc-clang
- Commit: cee249562eb0677b7d19b5b14c157b0f21345186

This note summarizes the issues hit on Windows while configuring and building the project, the fixes applied to the tree, and the most likely remaining blocker for the code generator (`gentest_codegen.exe`). It also lists concrete, minimal steps to try locally.

## Executive Summary

- We fixed multiple Windows-specific build problems (MSBuild batch wrapper failures, Linux-only link flags, PATH handling, incomplete codegen stubs, RTTI mismatch).
- The generator now builds and runs, but it still crashes early on Windows with exit code `0xC0000005` (access violation) during Clang Tooling.
- The remaining issue is most likely Clang not finding its builtin headers (the “resource directory”), which can cause crashes in `clang-cpp` on Windows when run outside the Clang install tree.

## Fixes Applied

The following changes are in the repository to improve Windows reliability:

1) gentest_codegen custom command and argument handling

- cmake/GentestCodegen.cmake
  - Add `--driver-mode=cl` on Windows so the tool uses MSVC discovery consistently.
  - Skip injecting massive `-isystem` include lists on Windows to avoid command length/batch wrapper problems (`NOT WIN32` guard).
  - Remove `COMMAND_EXPAND_LISTS` to prevent unintended PATH splitting in `cmake -E env` scenarios.
  - Remove the fragile PATH wrapper around the codegen invocation (rely on user PATH).
  - Add optional `-resource-dir` injection when `GENTEST_CLANG_RESOURCE_DIR` is set (helps Clang find builtin headers).

2) Terminfo shim (Linux-only) guarded for Windows

- tools/CMakeLists.txt
  - Only build/link the local `gentest_tinfo_shim` on UNIX. Prevents Windows link errors like `dl.lib` missing and `--version-script` unsupported.
  - Keep `GENTEST_USE_SYSTEM_TERMINFO=ON` as a supported path to avoid the shim.

3) RTTI (Windows/LLVM toolchain compatibility)

- tools/CMakeLists.txt
  - Do not force `-fno-rtti` for the generator. Official `clang-cpp` builds on Windows expect RTTI; forcing `-fno-rtti` can cause undefined behavior/crashes.

4) Robust codegen-time mocks to parse tests on any platform

- include/gentest/mock.h
  - Provide a `CodegenExpectationStub` under `GENTEST_CODEGEN` with the full fluent API used in tests: `.times()`, `.returns()`, `.invokes()`, `.allow_more()`, `.with()`, `.where_args()`, `.where()`, `.where_call()`.
  - Ensure `EXPECT_CALL` resolves by defining `using __gentest_target = T` in the codegen mock.
  - Handle abstract interfaces under `GENTEST_CODEGEN` by not deriving from `T` (to avoid “abstract class” instantiation), while still allowing `T* p = &mock` to parse.

5) Clang Tooling invocation stability & diagnostics

- tools/src/main.cpp
  - Stop suppressing Clang diagnostics; print discovered test/mocks counts and emit status.
  - Add `GENTEST_NO_MOCK_DISCOVERY` env switch to disable mock scanning (useful for isolation).
  - Prefer `clang-cl` as the fallback compiler on Windows instead of `clang++`.
  - Auto-inject `-resource-dir` on Windows at runtime when not already present: use `GENTEST_CLANG_RESOURCE_DIR` if set, else derive from `%LLVM_ROOT%\lib\clang\<ver>\include`.

## Originals Issues and Root Causes

1) MSBuild “The system cannot find the batch label specified - VCEnd”

- Root cause: MSBuild wraps very long custom commands in batch files with labels; our command was too long (huge `-isystem` list), causing the wrapper to break.
- Fix: Switch to Ninja (preferred on Windows) so custom commands don’t go through MSBuild batch wrappers. Also avoid injecting system include dirs on Windows.

2) Windows link errors: `LNK1104: cannot open file 'dl.lib'`, `/Wl,--version-script=... ignored`

- Root cause: building a Linux-only terminfo shim on Windows with POSIX flags.
- Fix: Guard the shim to UNIX and/or use `-DGENTEST_USE_SYSTEM_TERMINFO=ON` on Windows.

3) `cmake -E env` PATH splitting -> `no such file or directory`

- Root cause: `cmake -E env` treated the semicolon-delimited PATH as a CMake list, splitting it into many arguments, so the program to run became `C:\WINDOWS`.
- Fix: remove the wrapper entirely; rely on `PATH` being set once in the shell.

4) Codegen compilation errors inside tests (EXPECT_CALL/with/where/...)

- Root cause: codegen-time mocks were incomplete/invalid on Windows (local class with template members, missing fluent methods, missing `__gentest_target`).
- Fix: rework `include/gentest/mock.h` under `GENTEST_CODEGEN` as described above.

5) Generator runtime crash (0xC0000005) on Windows

- Very likely cause: Clang “resource dir” (builtin headers) not found reliably when running `clang-cpp` from a custom tool on Windows. Earlier verbose output showed a bogus resource dir under the build tree which did not exist.
- Mitigation in-tree:
  - Add `-resource-dir` pass-through in the custom command (when `GENTEST_CLANG_RESOURCE_DIR` is set).
  - Auto-detect the resource dir in `tools/src/main.cpp` from `GENTEST_CLANG_RESOURCE_DIR` or `%LLVM_ROOT%`.
  - Prefer `clang-cl` as the fallback compiler on Windows to align with MSVC discovery.

## Current Status and Likely Remaining Blocker

- The remaining crash is consistently `0xC0000005` during generator runs, even with mock discovery disabled, which indicates the failure is in the Clang frontend initialization/parsing rather than in our mock pass.
- This pattern is typical when `clang-cpp` cannot find its builtin headers on Windows. Passing the correct `-resource-dir` resolves this class of crashes.

## What to Try Locally (Minimal)

1) Ensure LLVM is on PATH for the shell running CMake/Ninja

- PowerShell:
  - `$env:Path = "$env:LLVM_ROOT\bin;$env:Path"`

2) Provide Clang’s resource dir to codegen

- PowerShell:
  - `$rd = & "$env:LLVM_ROOT\bin\clang.exe" -print-resource-dir`
  - `cmake -G "Ninja" -S . -B build -DLLVM_DIR="$env:LLVM_ROOT\lib\cmake\llvm" -DClang_DIR="$env:LLVM_ROOT\lib\cmake\clang" -DGENTEST_CLANG_RESOURCE_DIR="$rd"`
  - `ninja -C build -v`
- Verify each codegen call includes `-- -resource-dir "<...>\lib\clang\<ver>\include"` in `-v` output.

3) If still unstable, disable mock discovery to isolate

- PowerShell:
  - `$env:GENTEST_NO_MOCK_DISCOVERY = "1"`
  - `ninja -C build -v`

4) Manual single-file check (shows exact Clang setup)

- PowerShell:
  - `cd build\tests`
  - `..\tools\gentest_codegen.exe --check --compdb .. ..\..\tests\unit\cases.cpp -- --driver-mode=cl -std=c++23 -I..\..\include -I..\..\tests -v -resource-dir "$rd"`

## Optional: One-Pass Windows Preset (Recommended)

If you want a one-command workflow, add a Windows preset to `CMakePresets.json` like this (adjust `LLVM_ROOT` appropriately):

```jsonc
{
  "version": 6,
  "configurePresets": [
    {
      "name": "windows-clang",
      "generator": "Ninja",
      "cacheVariables": {
        "CMAKE_C_COMPILER": "$env{LLVM_ROOT}\\bin\\clang.exe",
        "CMAKE_CXX_COMPILER": "$env{LLVM_ROOT}\\bin\\clang-cl.exe",
        "LLVM_DIR": "$env{LLVM_ROOT}\\lib\\cmake\\llvm",
        "Clang_DIR": "$env{LLVM_ROOT}\\lib\\cmake\\clang",
        "GENTEST_USE_SYSTEM_TERMINFO": "ON"
      },
      "environment": {
        "PATH": "$env{LLVM_ROOT}\\bin;$penv{PATH}"
      }
    }
  ],
  "buildPresets": [
    {
      "name": "windows-clang",
      "configurePreset": "windows-clang",
      "environment": {
        "PATH": "$env{LLVM_ROOT}\\bin;$penv{PATH}"
      }
    }
  ],
  "testPresets": [
    {
      "name": "windows-clang",
      "configurePreset": "windows-clang"
    }
  ]
}
```

Then run:

```
cmake --preset=windows-clang
cmake --build --preset=windows-clang
ctest --preset=windows-clang --output-on-failure
```

## Change Log (Files Touched)

- cmake/GentestCodegen.cmake
- tools/CMakeLists.txt
- tools/src/main.cpp
- include/gentest/mock.h

## Appendix: Why These Failures Look the Way They Do

- “The system cannot find the batch label specified - VCEnd”: MSBuild’s batch wrapper for long custom commands breaks; Ninja avoids this.
- `LNK1104: dl.lib` / `--version-script`: Linux-only link setup attempted on Windows; gating to UNIX stops it.
- `cmake -E env` + PATH → “no such file or directory”: PATH semicolons were treated as a CMake list; removing the wrapper avoids it.
- 0xC0000005 (access violation) during codegen: typically indicates a `clang-cpp` frontend runtime problem on Windows, most commonly missing builtin headers. Passing `-resource-dir` with a real path resolves it.
