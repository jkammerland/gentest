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
- named-module consumer:
  - `gentest_consumer_module_meson`

The classic suites and textual consumer are part of the normal `meson compile`
and `meson test` flow. The module consumer is repo-local/manual:

- it is only defined when Meson reports `cpp.get_id()` as `clang` or `gcc`
- it is `build_by_default: false`
- it is not registered in `meson test`

## How the repo-local module path works

The checked-in module target is split in two stages:

1. `gentest_buildsystem_codegen.py --backend generic --kind modules` generates:
   - explicit mock metadata
   - generated mock module wrappers
   - a generated aggregate public module
   - the generated test module wrapper
2. `meson.build` compiles the resulting module graph with explicit
   `custom_target(...)` steps.

That distinction is intentional. The helper still rejects
`--backend meson --kind modules` on purpose, because there is no reusable
Meson-facing module helper surface yet. The repo-local checked-in module target
uses the generic helper backend and Meson-owned compilation steps instead.

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

Manual module consumer run:

```bash
meson compile -C build/meson gentest_consumer_module_meson
./build/meson/gentest_consumer_module_meson
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

The repo-local module consumer additionally writes module-specific artifacts
under `build/meson/`, including:

- `consumer_module_mocks_mock_metadata.json`
- `gentest/consumer_mocks.cppm`
- `suite_0000.cppm`
- `tu_0000_suite_0000.module.gentest.cppm`
- `tu_0000_suite_0000.gentest.h`
- staged defs/support files under `defs/` and `deps/`

## Limitations

- Repo-local only. There is no packaged Meson integration yet.
- There is no reusable Meson helper API for textual or module targets yet.
- The module path is limited to the checked-in consumer target pattern in
  [`meson.build`](../../meson.build).
- `meson test` currently validates the classic suites and the textual consumer,
  not the module consumer.
- The helper still fails fast for `--backend meson --kind modules`; that is an
  intentional boundary until a real Meson-facing module API exists.
