# Xmake

This Xmake integration is currently a repo-local convenience path for the
classic handwritten suites in this repository. It is not yet a general
downstream Xmake package API.

## Current scope

- Supports the classic suites in `tests/<suite>/cases.cpp`.
- Uses the shared per-TU helper in [`scripts/gentest_buildsystem_codegen.py`](../../scripts/gentest_buildsystem_codegen.py).
- Generates a classic wrapper TU plus `tu_*.gentest.h` registration header per suite.
- Does not currently support named-module suites, explicit mock targets, or an installed Xmake-facing `gentest_attach_codegen(...)` equivalent.

The currently wired suites are:

- `unit`
- `integration`
- `fixtures`
- `skiponly`

## Recommended usage

Build `gentest_codegen` with CMake first and point Xmake at it explicitly.
That is the most deterministic path.

Prerequisites:

- `cmake`
- `xmake`
- `python3` on Linux/macOS, `python` on Windows
- LLVM/Clang and the libraries needed to build `gentest_codegen`

Linux / macOS:

```bash
cmake --preset=host-codegen
cmake --build --preset=host-codegen --parallel

export GENTEST_CODEGEN="$PWD/build/host-codegen/tools/gentest_codegen"
xmake f -c -m release -o build/xmake
xmake b -a
xmake r gentest_unit_xmake
xmake r gentest_integration_xmake
xmake r gentest_fixtures_xmake
xmake r gentest_skiponly_xmake
```

Windows:

```powershell
cmake --preset=host-codegen
cmake --build --preset=host-codegen --parallel

$env:GENTEST_CODEGEN="$PWD\\build\\host-codegen\\tools\\gentest_codegen.exe"
xmake f -c -m release -o build/xmake
xmake b -a
xmake r gentest_unit_xmake
```

## Fallback generator bootstrap

If `GENTEST_CODEGEN` is not set, `xmake.lua` falls back to building the
generator with CMake under:

- `build/xmake-codegen/<host>/<arch>`

That fallback is intended as a convenience, not the preferred CI or local path.

## What Xmake generates

Per suite, Xmake writes generated files under:

- `<buildir>/gen/<plat>/<arch>/<mode>/<suite>/tu_0000_cases.gentest.cpp`
- `<buildir>/gen/<plat>/<arch>/<mode>/<suite>/tu_0000_cases.gentest.h`

For example, with `xmake f -o build/xmake ...`, the generated files land under:

- `build/xmake/gen/<plat>/<arch>/<mode>/<suite>/...`

That keeps different platform/arch/mode combinations from clobbering each
other's generated wrappers.

The resulting executables are:

- `gentest_unit_xmake`
- `gentest_integration_xmake`
- `gentest_fixtures_xmake`
- `gentest_skiponly_xmake`

## Adding another classic suite in this repo

1. Add `tests/<suite>/cases.cpp`.
2. Add another `gentest_suite("<suite>")` call in [`xmake.lua`](../../xmake.lua).
3. Re-run:

```bash
xmake b -a
xmake r gentest_<suite>_xmake
```

## Limitations

- This path is currently limited to the phase-1 classic-suite integration.
- It is intentionally limited to classic/header-style suites.
- If you need modules, explicit mock targets, package export, or a reusable
  consumer-facing integration, use the CMake path for now. Follow-up parity work
  is tracked in [`docs/stories/015_non_cmake_full_parity.md`](../stories/015_non_cmake_full_parity.md).
