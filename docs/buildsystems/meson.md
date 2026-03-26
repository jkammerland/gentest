# Meson

This Meson integration is currently a repo-local convenience path for the
classic handwritten suites in this repository. It is not yet a general
downstream Meson package API.

## Current scope

- Supports the classic suites in `tests/<suite>/cases.cpp`.
- Uses the shared per-TU helper in [`scripts/gentest_buildsystem_codegen.py`](../../scripts/gentest_buildsystem_codegen.py).
- Generates a classic wrapper TU plus `tu_*.gentest.h` registration header per suite.
- Does not currently support named-module suites, explicit mock targets, or an installed Meson-facing `gentest_attach_codegen(...)` equivalent.

The currently wired suites are:

- `unit`
- `integration`
- `fixtures`
- `skiponly`

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

## Adding another classic suite in this repo

1. Add `tests/<suite>/cases.cpp`.
2. Add `<suite>` to `tests_suites` in [`meson.build`](../../meson.build).
3. Re-run:

```bash
meson compile -C build/meson
meson test -C build/meson --print-errorlogs
```

## Limitations

- This path is currently limited to the phase-1 classic-suite integration.
- It is intentionally limited to classic/header-style suites.
- If you need modules, explicit mock targets, package export, or a reusable
  consumer-facing integration, use the CMake path for now. Follow-up parity work
  is tracked in [`docs/stories/015_non_cmake_full_parity.md`](../stories/015_non_cmake_full_parity.md).
