# Repository Guidelines

## Project Structure & Module Organization
`include/gentest/` exposes the public headers (`runner.h`, `attributes.h`) that other targets consume. The `src/` tree builds the sample `gentest` executable and publishes those headers through an interface library. Code generation lives in `tools/gentest_codegen`, our clang-tooling binary that scans annotated cases and emits `test_impl.cpp` sources. The helper macro wiring sits in `cmake/GentestCodegen.cmake`, and each suite under `tests/` combines handwritten `cases.cpp`, generated `test_impl.cpp`, and `support/test_entry.cpp`.

## Build, Test, and Development Commands
Configure a local build with `cmake --preset=debug`, then compile via `cmake --build --preset=debug`. Run the full test suite using `ctest --preset=debug --output-on-failure`. To refresh generated code for a suite, rebuild its target (e.g. `cmake --build --preset=debug --target gentest_unit_tests`). Sanitizer workflows are available through `cmake --workflow --preset=alusan` and reuse the same codegen pipeline.

## Coding Style & Naming Conventions
Follow the repo `.clang-format` (LLVM-derived, 4-space indent, 140-column limit) before committing. `.clang-tidy` checks `src`, `include`, and `tests`; run `ninja clang-tidy` from the build tree or invoke `clang-tidy` manually with the exported compilation database. Use lowercase snake_case for filenames, PascalCase for types, and camelCase for functions. Keep public symbols within the `gentest` namespace to avoid ABI mismatches.

## Testing Guidelines
Author suite cases in `tests/<suite>/cases.cpp` and annotate functions with `[[using gentest : test("suite/name"), ...]]` so the generator can discover them. Extend `tests/CMakeLists.txt` with `gentest_attach_codegen(TARGET ...)` to add generated sources automatically. Executables accept `--list` to enumerate cases and return non-zero on any `gentest::failure`; always run `ctest` locally prior to submitting changes.

## Commit & Pull Request Guidelines
Write short, imperative commit subjects (example: `Implement clang codegen attach helper`) and add body context when the change needs justification. Reference related issues or downstream consumers using trailers such as `Refs: #123`. Pull requests should outline scope, mention the build presets exercised (unit/integration and sanitizers when relevant), and include snippets of failing output when documenting regressions or flaky behavior.

## Tooling & Configuration Tips
Ensure `CMAKE_EXPORT_COMPILE_COMMANDS` remains enabled so `gentest_codegen` and tooling share an up-to-date compilation database. Keep your local environment aligned with `vcpkg.json` by letting CMake handle dependency bootstrapping, and pin any new packages in that manifest when required.
