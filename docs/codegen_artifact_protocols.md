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

## Textual Wrapper Registration

Textual `.cpp` tests use wrapper mode so source-local tests, anonymous
namespaces, `static` functions, and local fixture types stay visible to the
generated registration header.

A non-CMake adapter composes the existing protocol like this:

1. Predeclare:
   - `gen/tu_0000_cases.gentest.cpp`
   - `gen/tu_0000_cases.gentest.h`
   - `gen/my_tests.artifact_manifest.json`
   - `gen/my_tests.gentest.d`
2. Write the wrapper source before invoking codegen:

   ```cpp
   // gen/tu_0000_cases.gentest.cpp
   // NOLINTNEXTLINE(bugprone-suspicious-include)
   #include "../tests/cases.cpp"

   #if !defined(GENTEST_CODEGEN) && __has_include("tu_0000_cases.gentest.h")
   #include "tu_0000_cases.gentest.h"
   #endif
   ```

3. Run codegen with the wrapper as the input source and the real test file as
   the owner:

   ```bash
   gentest_codegen \
     --tu-out-dir gen \
     --tu-header-output gen/tu_0000_cases.gentest.h \
     --artifact-owner-source tests/cases.cpp \
     --artifact-manifest gen/my_tests.artifact_manifest.json \
     --compile-context-id my_tests:tests/cases.cpp \
     --depfile gen/my_tests.gentest.d \
     gen/tu_0000_cases.gentest.cpp \
     -- -std=c++20 -DGENTEST_CODEGEN=1 -I/path/to/gentest/include
   ```

4. Compile the wrapper source instead of the owner source. The manifest will
   declare `compile_as: "cxx-textual-wrapper"`,
   `target_attachment: "replace-owner-source"`,
   `includes_owner_source: true`, and `replaces_owner_source: true`.

Adapters may assert that the manifest contains the predeclared outputs and the
expected schema. They should not use generated JSON to invent new outputs after
their build graph has been finalized.

## Removed Legacy Manifest Mode

`gentest_codegen --output <file>` and
`gentest_attach_codegen(... OUTPUT <file>)` were removed in `2.0.0`. They now
hard-fail with migration guidance instead of producing a single generated
source that includes all inputs. `NO_INCLUDE_SOURCES`,
`GENTEST_NO_INCLUDE_SOURCES`, and `gentest_codegen --template <file>` were
removed with that mode.

Use textual wrapper registration with `--tu-out-dir` and explicit per-input
outputs. Build systems should predeclare wrapper `.cpp`/`.h` files, compile the
wrapper sources instead of owner sources, and optionally validate the generated
artifact manifest. The removal record is tracked in
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

## Same-Module Mock Registration

`MODULE_REGISTRATION` composes module-owned mocks by consuming the mock manifest
directly during registration emission. Build systems should predeclare the mock
manifest, run `inspect-mocks`, then pass that manifest to the registration
phase:

```bash
gentest_codegen inspect-mocks \
  --mock-manifest-output gen/tests.mock_manifest.json \
  --depfile gen/tests.mock_manifest.d \
  tests/provider.cppm tests/cases.cppm \
  -- -std=c++20 -I/path/to/gentest/include

gentest_codegen \
  --tu-out-dir gen \
  --tu-header-output gen/tu_0000_provider.gentest.h \
  --tu-header-output gen/tu_0001_cases.gentest.h \
  --module-registration-output gen/tu_0000_provider.registration.gentest.cpp \
  --module-registration-output gen/tu_0001_cases.registration.gentest.cpp \
  --artifact-manifest gen/tests.artifact_manifest.json \
  --mock-registration-manifest gen/tests.mock_manifest.json \
  --depfile gen/tests.gentest.d \
  tests/provider.cppm tests/cases.cppm \
  -- -std=c++20 -I/path/to/gentest/include
```

The registration phase validates the mock manifest schema, named-module output
domains, and owning module coverage. It emits same-module registration
implementation units that attach module-owned mocks without including the owning
`.cppm` source or importing the owning module. `emit-mocks` remains the
independent phase for final mock registry/implementation outputs; same-module
registration uses `--mock-registration-manifest` instead.

## Current Limits

Standalone declaration-only textual registration is not part of this protocol.
It was rejected in
[`docs/stories/036_textual_declaration_only_registration.md`](stories/036_textual_declaration_only_registration.md);
textual `.cpp` sources keep manifest-declared wrapper/include semantics, and
named modules remain the declaration-free registration path.

Full non-CMake parity across supported backends is tracked separately by
[`docs/stories/015_non_cmake_full_parity.md`](stories/015_non_cmake_full_parity.md).
