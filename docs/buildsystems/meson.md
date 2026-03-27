# Meson

This Meson support is still repo-local. The checked-in [`meson.build`](../../meson.build)
wires concrete targets for this repository; there is no installed Meson module
or reusable `gentest_add_mocks()` / `gentest_attach_codegen()` API yet.

## Current repo-local surface

Meson currently defines these targets:

- classic suites:
  - `gentest_unit_meson`
  - `gentest_integration_meson`
  - `gentest_fixtures_meson`
  - `gentest_skiponly_meson`
- textual explicit-mock consumer:
  - `gentest_consumer_textual_meson`

There is no checked-in Meson module consumer target. Meson is textual-only for
now.

## Module API status

The shared helper exposes an explicit module shape:

- `gentest_buildsystem_codegen.py --backend meson --mode suite --kind modules`
- `gentest_buildsystem_codegen.py --backend meson --mode mocks --kind modules`

Both entrypoints are intentional fail-fast boundaries today. They exist so the
Meson-facing API shape is explicit, but they stop immediately with a clear
error instead of pretending the backend can compile named modules reliably.

## Build and run

Prerequisites:

- `cmake`
- `meson`
- `ninja`
- `python3`
- LLVM/Clang development packages needed to build `gentest_codegen`

Build the host generator first:

```bash
cmake --preset=host-codegen
cmake --build --preset=host-codegen --parallel
```

Classic suites plus the textual consumer:

```bash
meson setup build/meson -Dcodegen_path=build/host-codegen/tools/gentest_codegen
meson compile -C build/meson
meson test -C build/meson --print-errorlogs
```

## Generated outputs

Classic suites still generate the usual per-TU wrapper pair in `build/meson/`:

- `tu_0000_<suite>_cases.gentest.cpp`
- `tu_0000_<suite>_cases.gentest.h`

The textual consumer also writes:

- `consumer_textual_mocks_defs.cpp`
- `consumer_textual_mocks_anchor.cpp`
- `tu_0000_consumer_textual_mocks_defs.gentest.h`
- `consumer_textual_mocks_mock_registry.hpp`
- `consumer_textual_mocks_mock_impl.hpp`
- `gentest_consumer_mocks.hpp`

## Limitations

- Repo-local only. There is no packaged Meson integration yet.
- There is no reusable Meson helper API for external/downstream projects yet.
- Meson named-module support is intentionally unsupported for now.
- The current repo-local Meson path still snapshots support headers/fragments at
  configure time. If you add new included support files, rerun
  `meson setup --reconfigure ...` before compiling again.
- The helper still fails fast for `--backend meson --kind modules`; that is the
  supported boundary until Meson module behavior is reliable enough to promote.
