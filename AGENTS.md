# Repository Guidelines

## Project Structure & Module Organization
- Public headers live in `include/gentest/` (`runner.h`, `attributes.h`) and are exposed via an interface library.
- The sample executable builds from `src/`. Code generation is in `tools/gentest_codegen` (a clang-tooling binary) that scans annotated cases and emits `test_impl.cpp`.
- Helper macro wiring is in `cmake/GentestCodegen.cmake`.
- Each suite under `tests/<suite>/` combines handwritten `cases.cpp`, generated `test_impl.cpp`, and `support/test_entry.cpp`.

## Build, Test, and Development Commands
- Preferred (system LLVM/Clang 20+):
  - `cmake --preset=debug-system`
  - `cmake --build --preset=debug-system`
  - `ctest --preset=debug-system --output-on-failure`
- Windows (dev machine):
  - Connect: `ssh ai-dev1@windows-11`
  - Repo path: `B:\repos\gentest`
- Windows (Clang/LLVM release packages + Ninja):
  - Pick the LLVM you want (examples use `C:\Tools\llvm-21.1.4` / `C:\Tools\llvm-20.1.8`), then from `B:\repos\gentest` in PowerShell:
    - `$llvm = 'C:\Tools\llvm-21.1.4'`
    - `$env:LLVM_BIN = "$llvm\bin"; $env:PATH = "$env:LLVM_BIN;$env:PATH"`
    - `$env:LLVM_DIR = "$llvm\lib\cmake\llvm"; $env:Clang_DIR = "$llvm\lib\cmake\clang"`
    - `cmake --preset=debug-system -DCMAKE_C_COMPILER="$env:LLVM_BIN\clang.exe" -DCMAKE_CXX_COMPILER="$env:LLVM_BIN\clang++.exe" -DLLVM_DIR="$env:LLVM_DIR" -DClang_DIR="$env:Clang_DIR"`
    - `cmake --build --preset=debug-system`
    - `ctest --preset=debug-system --output-on-failure`
- Windows (MSVC + LLVM tooling, Ninja):
  - From `B:\repos\gentest` in PowerShell (Developer prompt env required):
    - `& "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat" -arch=amd64`
    - `$msvcBuildDir = 'build\debug-system-msvc'`
    - `cmake -S . -B $msvcBuildDir -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -DGENTEST_ENABLE_PACKAGE_TESTS=ON -DLLVM_DIR="$env:LLVM_DIR" -DClang_DIR="$env:Clang_DIR"`
    - `cmake --build $msvcBuildDir`
    - `ctest --test-dir $msvcBuildDir --output-on-failure`
- Legacy vcpkg workflow:
  - `cmake --preset=debug`
  - `cmake --build --preset=debug`
  - `ctest --preset=debug --output-on-failure`
- Regenerate codegen: rebuild the relevant test target, e.g. `cmake --build --preset=debug --target gentest_unit_tests`.
- Sanitizers: `cmake --workflow --preset=alusan`.
- List tests: run the generated executable (e.g., `gentest_unit_tests --list`).

## Coding Style & Naming Conventions
- Follow `.clang-format` (LLVM-derived): 4-space indent, 140-column limit.
- Run `.clang-tidy` on `src`, `include`, `tests` via `ninja clang-tidy` in the build tree.
- Filenames: lowercase `snake_case`; types: `PascalCase`; functions: `camelCase`.
- Keep public symbols in the `gentest` namespace.

## Testing Guidelines
- Author cases in `tests/<suite>/cases.cpp` and annotate with `[[using gentest : test("suite/name"), ...]]` so the generator discovers them.
- Attach codegen in `tests/CMakeLists.txt` with `gentest_attach_codegen(TARGET <target>)`.
- Executables return non‑zero on any `gentest::failure`; always run `ctest` before pushing.

## Commit & Pull Request Guidelines
- Commits: short, imperative subject (e.g., “Implement clang codegen attach helper”); add context in the body when needed; use trailers like `Refs: #123`.
- PRs: describe scope, list exercised build presets (unit/integration and sanitizers), and include failing output snippets for regressions or flaky behavior.

## Tooling & Configuration Tips
- Keep `CMAKE_EXPORT_COMPILE_COMMANDS=ON` so `gentest_codegen` reuses the active compilation database.
- Let CMake manage dependencies via `vcpkg.json`; pin any new packages there.
- Cross-compiling (target = arm/riscv/etc, host runs codegen):
  - `GENTEST_BUILD_CODEGEN` defaults `OFF` when `CMAKE_CROSSCOMPILING=TRUE` (and also when `gentest_BUILD_TESTING=OFF`).
  - Build the host tool separately, then point the target build at it with `GENTEST_CODEGEN_EXECUTABLE`:
    - Host tool (native) build: `cmake -S . -B build/host -Dgentest_BUILD_TESTING=OFF -DGENTEST_BUILD_CODEGEN=ON && cmake --build build/host --target gentest_codegen`
    - Target build: `cmake -S <proj> -B build/target -DCMAKE_TOOLCHAIN_FILE=<toolchain.cmake> -DGENTEST_BUILD_CODEGEN=OFF -DGENTEST_CODEGEN_EXECUTABLE=<path-to-host-gentest_codegen>`
  - When consuming via `find_package(gentest CONFIG REQUIRED)`, `GentestCodegen.cmake` is included automatically; you still need to set `GENTEST_CODEGEN_EXECUTABLE` (or `GENTEST_CODEGEN_TARGET`) before calling `gentest_attach_codegen()`.
  - Future idea (not implemented): make the runtime fully header-only / consumer-built-from-sources and install `gentest_codegen` into the same prefix, then auto-set `GENTEST_CODEGEN_EXECUTABLE` relative to `gentestConfig.cmake` (single “devkit” prefix). Watch out for host-vs-target binary dependency pollution (e.g., `fmt`), and prefer documenting a consumer `ExternalProject_Add` pattern + adding a CI example test that validates the host-tool + target-build wiring.
- Notes for Windows + system LLVM:
  - `debug`/`release` presets require `VCPKG_ROOT` to be set; otherwise the toolchain file resolves to `/scripts/buildsystems/vcpkg.cmake` and configure fails.
  - When building with Clang on Windows, `cmake/GentestDependencies.cmake` aligns fmt + project flags to match typical prebuilt LLVM CRT/iterator settings and disables fmt's compile-time format checking (to avoid clang constant-eval issues).
  - LLVM 20 Windows packages may reference a non-existent `diaguids.lib` path; `tools/CMakeLists.txt` patches `LLVMDebugInfoPDB` to use an installed Visual Studio DIA SDK when found.
  - The `debug-system`/`release-system` presets enable `GENTEST_ENABLE_PACKAGE_TESTS` by default; disable with `-DGENTEST_ENABLE_PACKAGE_TESTS=OFF` if you want faster local runs.
  - Some Windows Debug CI environments hang on the two concurrency "death" tests; the CI sets `-DGENTEST_SKIP_WINDOWS_DEBUG_DEATH_TESTS=ON` (only disables `concurrency_fail_single_death` and `concurrency_multi_noadopt_death` in `Debug` on `WIN32`).
