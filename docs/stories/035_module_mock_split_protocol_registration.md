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

## Problem

The legacy module-wrapper path can transform module sources and inject
module-owned mock attachment code while running the monolithic
`--discover-mocks` flow. Story `035` replaced that path for same-module
registration: `MODULE_REGISTRATION` now runs split `inspect-mocks` discovery and
passes the generated manifest back to `gentest_codegen` with
`--mock-registration-manifest`.

## Protocol

The command shape and schema contract live in
[`docs/codegen_artifact_protocols.md#same-module-mock-registration`](../codegen_artifact_protocols.md#same-module-mock-registration).

## Acceptance Criteria

- `gentest_attach_codegen(... MODULE_REGISTRATION ...)` succeeds for
  module-owned mocks without falling back to module-wrapper source replacement.
- Generated same-module registration units contain `module <name>;` and do not
  include the owning `.cppm` source or import the owning module.
- The split mock protocol can run mock inspection and emission without test
  registration.
- Direct-tool and CMake regressions prove module-owned mock attachment is driven
  by tool-owned protocol data.
- Existing explicit mock, module mock, and story `034` module-registration
  regressions remain green.
