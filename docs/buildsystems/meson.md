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
- The shared helper now takes an explicit `kind`:
  - `kind=textual` works
  - `kind=modules` exists as an API shape but fails fast on Meson for now
- Generates a classic wrapper TU plus `tu_*.gentest.h` registration header per suite.
- Does not currently support named-module suites, module mock defs, or an installed Meson-facing `add_mocks(...)` / `attach_codegen(...)` API yet.

The currently wired suites are:

- `unit`
- `integration`
- `fixtures`
- `skiponly`

The additional repo-local explicit textual mock test is:

- `meson_consumer_textual`

## API shape

The intended Meson-facing model is still just two operations:

```meson
gentest_add_mocks(
  'demo_mocks',
  kind: 'textual',
  defs: files('tests/mocks.hpp'),
  output_dir: 'build/gentest/demo_mocks',
  header_name: 'generated/demo_mocks.hpp',
)

gentest_attach_codegen(
  'demo_tests',
  kind: 'textual',
  sources: files('tests/cases.cpp'),
  output_dir: 'build/gentest/demo_tests',
  deps: [demo_mocks],
)
```

For named modules, the API shape is explicit too, but Meson intentionally
rejects it today instead of pretending it works:

```meson
gentest_add_mocks(
  'demo_mocks',
  kind: 'modules',
  defs: files('tests/service.cppm', 'tests/mocks.cppm'),
  output_dir: 'build/gentest/demo_mocks',
  module_name: 'demo.mocks',
)
```

That should fail with a clear diagnostic telling you to use `kind: 'textual'`
for Meson for now, or use CMake/Xmake for named modules.

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
- It is intentionally limited to textual/header-style suites and textual mock
  defs.
- The modules-shaped API exists, but Meson `kind=modules` requests fail fast by
  design for now.
- If you need real named modules, module mock defs, or package/export parity,
  use the CMake path today. Follow-up parity work is tracked in
  [`docs/stories/015_non_cmake_full_parity.md`](../stories/015_non_cmake_full_parity.md).
