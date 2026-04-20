# Story: replace module-wrapper mock injection with split mock registration

## Status

Done.

- `gentest_codegen` now accepts `--mock-registration-manifest` in same-module
  registration mode and consumes named-module mock metadata without reparsing
  mock definition sources during registration emission.
- CMake `MODULE_REGISTRATION` targets now run a tool-owned `inspect-mocks`
  phase, pass the resulting manifest into the registration phase, and keep
  CMake out of mock JSON/source semantics.
- Direct-tool and CMake regressions cover generated same-module mock
  attachment without including the owning `.cppm` source or importing the
  owning module.
- The broader cleanup campaign still tracks removal of legacy module-wrapper
  source transformation and related non-registration compatibility plumbing
  after downstream wrapper-mode users are migrated.

## Goal

Retire same-module `MODULE_REGISTRATION`'s dependency on the integrated
module-wrapper mock attachment path by making registration consume the split
`inspect-mocks` manifest through `--mock-registration-manifest`.
`gentest_codegen` must continue to own mock semantics, module-domain
classification, and generated attachment shape. Build-system adapters may
predeclare paths and pass manifests, but must not parse C++ or generated JSON
for planning. `emit-mocks` remains an independent phase for final mock outputs;
it is not the registration handoff.

## Problem

The legacy module-wrapper path can transform module sources and inject
module-owned mock attachment code while running the monolithic
`--discover-mocks` flow. Story `035` replaced that path for same-module
registration: `MODULE_REGISTRATION` now runs split `inspect-mocks` discovery and
passes the generated manifest back to `gentest_codegen` with
`--mock-registration-manifest`.

## Scope

In scope:

- define the manifest or registration handoff needed for module-owned mock
  attachments without reparsing mock definition sources
- make same-module registration consume split mock outputs or an explicit mock
  manifest where module-owned attachment is supported
- keep generated registration as additive same-module implementation units;
  do not reintroduce `#include "<source>.cppm"` or `import <owning-module>` as
  the registration mechanism
- preserve `inspect-mocks` and `emit-mocks` as independently runnable phases
- keep CMake as a thin adapter that predeclares outputs and passes tool-owned
  protocol files/paths

Out of scope:

- declaration-only textual registration
- full non-CMake backend parity
- CMake JSON parsing for build-graph planning
- source-text module or mock classification outside `gentest_codegen`

## Example Target Shape

```text
gentest_codegen inspect-mocks
  --mock-manifest-output gen/tests.mock_manifest.json
  tests/module_mock_defs.cppm tests/cases.cppm
  -- -std=c++20 ...

gentest_codegen emit-mocks
  --mock-manifest-input gen/tests.mock_manifest.json
  --mock-registry gen/tests_mock_registry.hpp
  --mock-impl gen/tests_mock_impl.hpp
  --mock-domain-registry-output gen/tests_mock_registry__domain_0000_header.hpp
  --mock-domain-registry-output gen/tests_mock_registry__domain_0001_my_tests.hpp
  --mock-domain-impl-output gen/tests_mock_impl__domain_0000_header.hpp
  --mock-domain-impl-output gen/tests_mock_impl__domain_0001_my_tests.hpp

gentest_codegen
  --tu-out-dir gen
  --module-registration-output gen/tu_0000_cases.registration.gentest.cpp
  --artifact-manifest gen/tests.artifact_manifest.json
  --mock-registration-manifest gen/tests.mock_manifest.json
  tests/cases.cppm -- -std=c++20 ...
```

## Acceptance Criteria

- A `gentest_attach_codegen(... MODULE_REGISTRATION ...)` module test that uses
  module-owned mocks succeeds without falling back to module-wrapper source
  replacement.
- Generated same-module registration units still contain `module <name>;` and
  do not include the owning `.cppm` source or import the owning module.
- The split mock protocol can still run mock inspection and emission without
  test registration.
- A focused direct-tool or CMake regression proves module-owned mock attachment
  is driven by tool-owned protocol data, not CMake source parsing.
- Existing explicit mock, module mock, and story `034` module-registration
  regressions remain green.
