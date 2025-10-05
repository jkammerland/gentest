# Repository Guidelines

## Project Structure & Module Organization
- `include/gentest/` contains the public surface: `runner.h` (assert helpers and API contract) and `attributes.h` (annotation shim).
- `src/` builds the sample `gentest` executable and exposes headers via an interface library for reuse in tests/tools.
- `tools/gentest_codegen` is the clang-tooling binary that discovers annotated functions and emits `test_impl.cpp` files.
- `cmake/GentestCodegen.cmake` provides the `gentest_attach_codegen()` helper used by test targets to add the generator output.
- `tests/` is organised by suite (`unit`, `integration`, …); each suite pairs its `cases.cpp` with a generated `test_impl.cpp` plus the shared `support/test_entry.cpp`.

## Build, Test, and Development Commands
- Preferred workflow (system LLVM/Clang 20+): `cmake --preset=debug-system`, `cmake --build --preset=debug-system`, then `ctest --preset=debug-system --output-on-failure`.
- Legacy vcpkg workflow (downloads its own LLVM toolchain): `cmake --preset=debug`, `cmake --build --preset=debug`, then `ctest --preset=debug --output-on-failure`.
- Regenerate codegen artefacts by rebuilding the relevant test target (`cmake --build --preset=debug --target gentest_unit_tests`).
- Sanitizer runs are exposed via presets (`cmake --workflow --preset=alusan`) and reuse the same code-generation path.
- Keep `compile_commands.json` enabled (`CMAKE_EXPORT_COMPILE_COMMANDS`) so `gentest_codegen` can reuse the active compilation database.

## Coding Style & Naming Conventions
- C++ formatting follows the LLVM-derived `.clang-format` (4-space indent, 140 column limit). Format touched files before submitting changes.
- `.clang-tidy` targets `src`, `include`, and `tests`; run `ninja clang-tidy` from the configured build tree or invoke `clang-tidy` manually.
- Use lowercase snake_case for filenames, PascalCase for types, and camelCase for functions. Namespaces should remain succinct (`unit`, `integration`, `gentest::detail`, etc.).
- Keep attribute helpers and runtime symbols under the `gentest` namespace to preserve linkage clarity.

## Testing Guidelines
- Each suite requires a `cases.cpp` with annotated functions and leverages the shared `support/test_entry.cpp` main that calls `gentest::run_all_tests`.
- Annotate tests with `[[using gentest : test("suite/name"), ...]]`. The `test("...")` entry supplies the manifest name;
  additional tokens (e.g. `slow`, `req("BUG-1")`, `skip("ci only")`) are collected for future filtering.
- Extend `tests/CMakeLists.txt` via `gentest_attach_codegen(TARGET ...)` so the generator runs before compilation and the produced `test_impl.cpp` is added automatically.
- Generated code supports `--list` to print available cases and returns non-zero on any `gentest::failure`; verify both local suites with `ctest` before submitting.

## Commit & Pull Request Guidelines
- Use short, imperative commit messages (`Implement clang codegen attach helper`). Provide additional detail in the body when rationale spans more than the subject line.
- Reference related issues or downstream tooling changes in commit trailers (e.g. `Refs: #123`).
- Pull requests should outline scope, mention the presets exercised (unit/integration plus any sanitizers), and include snippets of failing output when discussing regressions.
