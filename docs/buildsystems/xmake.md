# Xmake

This Xmake integration is currently a repo-local convenience path for this
repository. It covers the classic handwritten suites, and it has an in-tree
textual explicit-mock slice under active stabilization, but it is not yet a
general downstream Xmake package API.

## Current scope

- Supports the classic suites in `tests/<suite>/cases.cpp`.
- Has one repo-local explicit textual mock slice under active stabilization:
  - defs file: `tests/consumer/header_mock_defs.hpp`
  - generated public header: `build/xmake/gen/<plat>/<arch>/<mode>/consumer_textual_mocks/gentest_consumer_mocks.hpp`
  - consumer test source: `tests/buildsystems/consumer_textual_cases.cpp`
- Uses the shared per-TU helper in [`scripts/gentest_buildsystem_codegen.py`](../../scripts/gentest_buildsystem_codegen.py).
- Generates a classic wrapper TU plus `tu_*.gentest.h` registration header per suite.
- Does not currently support named-module suites, module mock defs, or an installed Xmake-facing `add_mocks(...)` / `attach_codegen(...)` API yet.

The currently wired suites are:

- `unit`
- `integration`
- `fixtures`
- `skiponly`

The additional repo-local explicit textual mock target is:

- `gentest_consumer_textual_xmake`

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
xmake r gentest_consumer_textual_xmake
```

Windows:

```powershell
cmake --preset=host-codegen
cmake --build --preset=host-codegen --parallel

$env:GENTEST_CODEGEN="$PWD\\build\\host-codegen\\tools\\gentest_codegen.exe"
xmake f -c -m release -o build/xmake
xmake b -a
xmake r gentest_unit_xmake
xmake r gentest_consumer_textual_xmake
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
- `gentest_consumer_textual_xmake`

For the textual mock slice, Xmake also writes generated files under:

- `<buildir>/gen/<plat>/<arch>/<mode>/consumer_textual_mocks/consumer_textual_mocks_defs.cpp`
- `<buildir>/gen/<plat>/<arch>/<mode>/consumer_textual_mocks/consumer_textual_mocks_anchor.cpp`
- `<buildir>/gen/<plat>/<arch>/<mode>/consumer_textual_mocks/tu_0000_consumer_textual_mocks_defs.gentest.h`
- `<buildir>/gen/<plat>/<arch>/<mode>/consumer_textual_mocks/gentest_consumer_mocks.hpp`
- `<buildir>/gen/<plat>/<arch>/<mode>/consumer_textual_mocks/def_0000_header_mock_defs.hpp`
- domain-specific generated mock headers under `<buildir>/gen/<plat>/<arch>/<mode>/consumer_textual_mocks/`

The consumer executable then compiles:

- `<buildir>/gen/<plat>/<arch>/<mode>/consumer_textual/tu_0000_consumer_textual_cases.gentest.cpp`

## Adding another classic suite in this repo

1. Add `tests/<suite>/cases.cpp`.
2. Add another `gentest_suite("<suite>")` call in [`xmake.lua`](../../xmake.lua).
3. Re-run:

```bash
xmake b -a
xmake r gentest_<suite>_xmake
```

## Limitations

- This path currently supports:
  - classic per-TU suites
  - one in-tree textual explicit-mock slice under active stabilization
- It is intentionally limited to classic/header-style suites.
- If you need named modules, module mock defs, reusable/public Xmake
  `add_mocks(...)` / `attach_codegen(...)` helpers, or package/export parity,
  use the CMake path for now. Follow-up parity work is tracked in
  [`docs/stories/015_non_cmake_full_parity.md`](../stories/015_non_cmake_full_parity.md).
