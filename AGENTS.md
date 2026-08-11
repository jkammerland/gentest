# Repository Guidelines

## Quick start (system LLVM/Clang 20+)

```bash
cmake --preset=debug-system && cmake --build --preset=debug-system && ctest --preset=debug-system --output-on-failure
```

Other presets: `release`, `release-system`, `alusan` (ASan+LSan+UBSan), `coverage-system`, `tsan`, `tidy`, `host-codegen` (cross-compile helper). See `CMakePresets.json` for the full list.

## What this repo is

`gentest` is an attribute-driven C++ test runner with a clang-tooling code generator. Tests are annotated with standard C++ attributes (`[[using gentest: test("suite/name")]]`) instead of macros. The codegen tool (`gentest_codegen`) scans sources via `compile_commands.json` and emits registration wrappers.

## Key directories

| Path | Contents |
|---|---|
| `include/gentest/` | Public headers (`runner.h`, `attributes.h`, `fixture.h`, `async.h`) + named-module interface units (`.cppm`) |
| `src/` | Runtime implementation (`runner_impl.cpp`) |
| `tools/` | `gentest_codegen` clang-tooling binary + CMake helper (`cmake/GentestCodegen.cmake`) |
| `tests/<suite>/` | Handwritten `cases.cpp` per suite (unit, integration, fixtures, mocking, async, benchmarks, etc.) |
| `tests/support/` | Shared test entry point |
| `cmake/` | CMake modules, toolchains, regression-check scripts |
| `docs/` | User-facing docs (index at `docs/index.md`) |
| `examples/` | Copy-paste consumer projects (`hello`, `hello_modules`) |

## Writing tests

- Annotate with `[[using gentest: test("suite/name"), ...]]` (or `[[gentest::test]]` for single-attribute shorthand).
- Place cases in `tests/<suite>/cases.cpp`.
- Attach codegen in `tests/CMakeLists.txt` via `gentest_attach_codegen(<target>)`.
- Update suite counts (`*_counts`, `*_list_counts`, `*_list_tests_lines`) when adding tests.
- Test executables return non-zero on any failure — always run `ctest` before pushing.

## Fixture model

- Fixture lifetimes: **local** (per test), **suite** (per namespace group), **global** (per run).
- Fixture parameters are inferred from free-function signatures (`T&`, `T*`, `std::shared_ptr<T>`).
- Suite/global fixtures are scoped to their declaring namespace and descendants; declare in the common ancestor.
- Prefer free-function tests. Member tests are deprecated.
- Allocation hook: optional `static gentest_allocate()` — returns `unique_ptr`, `shared_ptr`, or raw `T*` (adopted).

## Mocks

Declare mock definitions in a header, then create an explicit mock target:

```cmake
gentest_add_mocks(clock_mocks
  DEFS ${CMAKE_CURRENT_SOURCE_DIR}/clock_mocks.hpp
  OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/mocks"
  HEADER_NAME public/clock_mocks.hpp)
target_link_libraries(my_tests PRIVATE gentest::gentest_main clock_mocks)
gentest_attach_codegen(my_tests)
```

Link mock targets **before** `gentest_attach_codegen()` so codegen sees the surface during discovery.

## Codegen quirks

- `gentest_codegen` reads `compile_commands.json` — keep `CMAKE_EXPORT_COMPILE_COMMANDS=ON` (it is by default).
- Per-TU registration mode (default): generates `tu_*.gentest.h` headers and CMake shim TUs. Requires a **single-config** generator (Ninja). Multi-config (VS, Xcode) → use `OUTPUT` (manifest mode).
- Cross-compiling: build a host `gentest_codegen` first, then set `GENTEST_CODEGEN_EXECUTABLE` in the target build.
- Public modules: `GENTEST_ENABLE_PUBLIC_MODULES=AUTO|ON|OFF` (default `OFF`).
- `gentest_codegen --jobs=<N>` / `GENTEST_CODEGEN_JOBS=<N>` controls parallelism (threads inside one process).

## Style

- `.clang-format` (LLVM-derived): 4-space indent, 140-column limit.
- Run `clang-format -i <paths>` on edited files. CI gate: `scripts/check_clang_format.sh`.
- Filenames: `snake_case`. Types: `PascalCase`. Functions: `camelCase`.
- Public symbols in the `gentest` namespace.

## Static analysis & coverage

```bash
# clang-tidy (CI-aligned script)
cmake --preset=debug-system
scripts/check_clang_tidy.sh build/debug-system

# Coverage
python3 -m pip install --upgrade gcovr==8.6
cmake --preset=coverage-system -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build --preset=coverage-system
find build/coverage-system -name '*.gcda' -delete
rm -rf build/coverage-system/coverage-report
ctest --preset=coverage-system --output-on-failure --parallel 1
python3 scripts/coverage_hygiene.py --build-dir build/coverage-system --ignore-statuses stamp_mismatch --gcov llvm-cov gcov
python3 scripts/coverage_report.py --build-dir build/coverage-system
```

## Gotchas

- `debug`/`release` presets require `VCPKG_ROOT` set. `debug-system` does not.
- `debug-system`/`release-system` enable `GENTEST_ENABLE_PACKAGE_TESTS` by default (slower); disable with `-DGENTEST_ENABLE_PACKAGE_TESTS=OFF`.
- Windows Debug CI: two concurrency death tests hang. CI passes `-DGENTEST_SKIP_WINDOWS_DEBUG_DEATH_TESTS=ON`.
- Async tests with unresumable fixtures block forever — if adopted work detaches or never releases `CurrentContextLease`, the runner hangs.
- `gentest::log()` and cooperative stop checks in spawned threads/coroutines require setting the current context: `auto adoption = gentest::set_current_context(gentest::get_current_context())`.

## Conventions

- Commits: Conventional Commits (`type(scope): subject`).
- PRs: describe scope, list exercised build presets, include failing output for regressions.
- Dependencies: pin in `vcpkg.json`.
- Docs: start at `docs/index.md`. Changelog at `CHANGELOG.md`, deprecations at `DEPRECATIONS.md`.
