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

