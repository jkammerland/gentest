# Meson

This Meson integration is currently a repo-local convenience path for this
repository. It is not yet a general downstream Meson package API.

## Current scope

- Supports the classic suites in `tests/<suite>/cases.cpp`.
- Supports one repo-local explicit textual mock slice:
  - defs file: `tests/consumer/header_mock_defs.hpp`
  - generated public header: `gentest_consumer_mocks.hpp`
  - consumer test source: `tests/buildsystems/consumer_textual_cases.cpp`
- Uses the shared per-TU helper in [`scripts/gentest_buildsystem_codegen.py`](../../scripts/gentest_buildsystem_codegen.py).
- Generates a classic wrapper TU plus `tu_*.gentest.h` registration header per suite.
- Does not currently support named-module suites, module mock defs, or an installed Meson-facing `add_mocks(...)` / `attach_codegen(...)` API yet.

The currently wired suites are:

- `unit`
- `integration`
- `fixtures`
- `skiponly`

The additional repo-local explicit textual mock test is:

- `meson_consumer_textual`

## Prerequisites

- `cmake`
- `meson`
- `ninja`
- `python3`
- LLVM/Clang and the libraries needed to build `gentest_codegen`

Build the host generator first:

```bash
cmake --preset=host-codegen
cmake --build --preset=host-codegen --parallel
```

## Build and run

Linux / macOS:

```bash
meson setup build/meson -Dcodegen_path=build/host-codegen/tools/gentest_codegen
meson compile -C build/meson
meson test -C build/meson --print-errorlogs
```

Windows:

```powershell
meson setup build/meson -Dcodegen_path=build/host-codegen/tools/gentest_codegen.exe
meson compile -C build/meson
meson test -C build/meson --print-errorlogs
```

## What Meson generates

For each suite, Meson runs the shared helper and produces:

- `build/meson/tu_0000_<suite>_cases.gentest.cpp`
- `build/meson/tu_0000_<suite>_cases.gentest.h`

The wrapper `.cpp` includes the original `tests/<suite>/cases.cpp` first and
then includes the generated header when it exists, matching the CMake TU-wrapper
model.

The resulting executables are:

- `gentest_unit_meson`
- `gentest_integration_meson`
- `gentest_fixtures_meson`
- `gentest_skiponly_meson`
- `gentest_consumer_textual_meson`

For the textual mock slice, Meson also generates:

- `build/meson/consumer_textual_mocks_defs.cpp`
- `build/meson/consumer_textual_mocks_anchor.cpp`
- `build/meson/tu_0000_consumer_textual_mocks_defs.gentest.h`
- `build/meson/consumer_textual_mocks_mock_registry.hpp`
- `build/meson/consumer_textual_mocks_mock_impl.hpp`
- `build/meson/consumer_textual_mocks_mock_registry__domain_0000_header.hpp`
- `build/meson/consumer_textual_mocks_mock_impl__domain_0000_header.hpp`
- `build/meson/def_0000_header_mock_defs.hpp`
- `build/meson/gentest_consumer_mocks.hpp`

The consumer source [`tests/buildsystems/consumer_textual_cases.cpp`](../../tests/buildsystems/consumer_textual_cases.cpp)
then includes the generated public header and links the generated mock library.

## Adding another classic suite in this repo

1. Add `tests/<suite>/cases.cpp`.
2. Add `<suite>` to `tests_suites` in [`meson.build`](../../meson.build).
3. Re-run:

```bash
meson compile -C build/meson
meson test -C build/meson --print-errorlogs
```

## Limitations

- This path currently supports:
  - classic per-TU suites
  - one in-tree textual explicit-mock slice
- It is still intentionally limited to classic/header-style suites.
- If you need named modules, module mock defs, reusable/public Meson-facing
  `add_mocks(...)` / `attach_codegen(...)` helpers, or package/export parity,
  use the CMake path for now. Follow-up parity work is tracked in
  [`docs/stories/015_non_cmake_full_parity.md`](../stories/015_non_cmake_full_parity.md).
