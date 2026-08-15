# gentest_codegen Artifact Protocols

This page documents the currently supported machine-readable codegen
protocols. The schema identifiers are part of the contract:

- Artifact manifest: `gentest.artifact_manifest.v1`
  ([schema](schemas/gentest.artifact_manifest.v1.schema.json))
- Mock manifest: `gentest.mock_manifest.v1`
  ([schema](schemas/gentest.mock_manifest.v1.schema.json))

Build systems should predeclare concrete outputs, pass those paths to
`gentest_codegen`, and treat generated manifests as validation/product files.
They should not parse C++ sources to classify modules, discover mocks, or infer
registration semantics.

## Textual Header-Declaration Registration

Ordinary CMake `gentest_attach_codegen(target)` uses additive registration.
Build integration predeclares one target-unique generated `.cpp` per authored
translation unit and one fallback slot per explicitly listed header. Generated
slot sources are appended to the target, marked generated, and excluded from
unity aggregation. Authored `.cpp` files remain attached and retain their exact
compilation-database commands.

The internal `--textual-registration-output` and `--scan-slot-kind` arguments
are build-owned plumbing, not user source IDs or ownership controls. Each scan
slot is paired with the generated output's unique compile command and retargeted
to its authored input for semantic Clang scanning. Parallel workers write only
their indexed result; a serial merge validates declaration sites, C++ entity
identity, semantic fingerprints, fixture dependencies, and stable target-local
ownership before parallel emission. Unassigned outputs contain a valid empty
source.

The artifact manifest records every predeclared slot as
`cxx-header-declaration-registration`, its scan source and slot kind, and a
`sha256:` normalized semantic compile-context fingerprint. The same
fingerprint is attached to the generated artifact, whose contract is
`target_attachment: "append-generated-source"`,
`includes_authored_source: false`, and `replaces_authored_source: false`.
Codegen computes the fingerprint from the target-unique generated compile
command, retargets that command to the scan source, and verifies that the
actual adjusted host-Clang invocation retains every selected semantic token in
order. It fails if retargeting or host-tool adjustment loses context.

Non-empty outputs include only the selected annotated headers and generated
runtime support. They never include an authored `.cpp`. Definitions are
resolved by ordinary linking.

## Removed Source-Including Modes

`gentest_codegen --output <file>`, `--textual-wrapper-output`, and
`--artifact-owner-source`, plus
`gentest_attach_codegen(... OUTPUT <file>)` were removed in `2.0.0`. They now
hard-fail with migration guidance instead of producing a single generated
source that includes all inputs. `NO_INCLUDE_SOURCES`,
`GENTEST_NO_INCLUDE_SOURCES`, and `gentest_codegen --template <file>` were
removed with that mode.

Use additive header-declaration registration with predeclared per-slot `.cpp`
outputs. Build systems keep owner sources, append generated sources, and retain
reachable-header depfiles. The removal record is tracked in
[`DEPRECATIONS.md`](../DEPRECATIONS.md).

## Mock Manifest Phases

Mocks use a separate manifest. Discovery and emission are independent phases:

```bash
gentest_codegen inspect-mocks \
  --mock-manifest-output gen/service.mock_manifest.json \
  tests/mock_defs.cpp \
  -- -std=c++20 -I/path/to/gentest/include -Itests

gentest_codegen emit-mocks \
  --mock-manifest-input gen/service.mock_manifest.json \
  --mock-registry gen/service_mock_registry.hpp \
  --mock-impl gen/service_mock_impl.hpp \
  --mock-domain-registry-output gen/service_mock_registry__domain_0000_header.hpp \
  --mock-domain-impl-output gen/service_mock_impl__domain_0000_header.hpp \
  --depfile gen/service_mock_codegen.d
```

For named-module mocks, the manifest must declare
`mock_output_domain_modules` containing only named-module domains in domain
order. The header-like aggregate domain is implicit and always consumes the
first `--mock-domain-*` output slot; named-module domains consume the following
slots in manifest order. A build-system adapter that already owns the module
list can predeclare those outputs and pass them to `emit-mocks` without parsing
generated JSON for planning:

```bash
gentest_codegen inspect-mocks \
  --mock-manifest-output gen/module_mocks.mock_manifest.json \
  tests/service.cppm tests/module_mock_defs.cppm \
  -- -std=c++20 -I/path/to/gentest/include

gentest_codegen emit-mocks \
  --mock-manifest-input gen/module_mocks.mock_manifest.json \
  --mock-registry gen/module_mock_registry.hpp \
  --mock-impl gen/module_mock_impl.hpp \
  --mock-domain-registry-output gen/module_mock_registry__domain_0000_header.hpp \
  --mock-domain-registry-output gen/module_mock_registry__domain_0001_service.hpp \
  --mock-domain-impl-output gen/module_mock_impl__domain_0000_header.hpp \
  --mock-domain-impl-output gen/module_mock_impl__domain_0001_service.hpp
```

`emit-mocks` validates that every named-module mock belongs to a manifest
module domain and rejects unsupported `schema` values.

## Explicit Mock Aggregate Modules

CMake explicit mock targets with module `DEFS` predeclare a public aggregate
module interface and ask `gentest_codegen` to emit it:

```bash
gentest_codegen \
  --tu-out-dir gen \
  --tu-header-output gen/tu_0000_service.gentest.h \
  --module-wrapper-output gen/tu_0000_service.module.gentest.cppm \
  --tu-header-output gen/tu_0001_module_mocks.gentest.h \
  --module-wrapper-output gen/tu_0001_module_mocks.module.gentest.cppm \
  --mock-registry gen/module_mock_registry.hpp \
  --mock-impl gen/module_mock_impl.hpp \
  --mock-domain-registry-output gen/module_mock_registry__domain_0000_header.hpp \
  --mock-domain-registry-output gen/module_mock_registry__domain_0001_service.hpp \
  --mock-domain-impl-output gen/module_mock_impl__domain_0000_header.hpp \
  --mock-domain-impl-output gen/module_mock_impl__domain_0001_service.hpp \
  --mock-aggregate-module-name fixture.explicit_module_mocks \
  --mock-aggregate-module-output gen/fixture/explicit_module_mocks.cppm \
  tests/service.cppm tests/module_mocks.cppm \
  -- -std=c++20 -I/path/to/gentest/include
```

The aggregate output is a build-owned product and is listed in the depfile.
It re-exports `gentest`, `gentest.mock`, and the discovered named-module
domains. Build-system adapters should not parse the module `DEFS` at
configure time to write this file themselves.

When an installed explicit mock target is consumed later, the buildsystem may
only have provisional names for generated module-wrapper outputs. It should
still pass those files with `--external-module-source=<name>=<path>`.
`gentest_codegen` reads each candidate and only uses it for the requested
module when the source declares that module name.

## Named-Module Importer Registration

`MODULE_REGISTRATION` scans selected primary module interfaces and emits one
ordinary importer translation unit per input. Cases and adapter-visible fixture
types must be exported; module-internal entities are rejected. Build systems
predeclare the generated header, importer source, manifest, and depfile:

```bash
gentest_codegen \
  --tu-out-dir gen \
  --tu-header-output gen/tu_0000_cases.gentest.h \
  --module-registration-output gen/tu_0000_cases.registration.gentest.cpp \
  --artifact-manifest gen/tests.artifact_manifest.json \
  --depfile gen/tests.gentest.d \
  tests/cases.cppm \
  -- -std=c++20 -I/path/to/gentest/include
```

The generated source includes registration support, imports the owning module,
and then includes the generated registration header with its preamble disabled.
It never includes the authored `.cppm` and never declares `module M;`. Its
artifact entry uses `compile_as: "cxx-module-importer-registration"`, records
`imports: ["M"]`, and keeps `requires_module_scan: true`.

Direct module-owned `gentest::mock<T>` fixture parameters are rejected. Use an
explicit mock target that publishes a generated module surface, import that
surface from the case module, and use the published mock inside the test body.

## Current Limits

Textual annotations and generated-adapter dependencies must be
header-reachable. Source-only annotations, internal-linkage cases, and active
named-module imports in a textual scan context are rejected. Module-authored
tests use exported importer registration as documented in
[`docs/modules.md`](modules.md). The superseded decision history is recorded in
[`docs/stories/036_textual_declaration_only_registration.md`](stories/036_textual_declaration_only_registration.md).

Full non-CMake parity across supported backends is tracked separately by
[`docs/stories/015_non_cmake_full_parity.md`](stories/015_non_cmake_full_parity.md).
