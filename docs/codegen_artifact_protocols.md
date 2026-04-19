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

## Legacy Manifest Mode

`gentest_codegen --output <file>` emits one generated source file that includes
all input sources. In CMake this is selected with
`gentest_attach_codegen(... OUTPUT <file>)`. This mode is deprecated and kept as
a fallback for multi-config generators, bootstrapping, and constrained build
graphs that cannot yet predeclare per-TU wrapper artifacts.

New integrations should use textual wrapper registration with `--tu-out-dir`
and explicit per-input outputs. Existing manifest-mode users should migrate by
predeclaring wrapper `.cpp`/`.h` files, compiling the wrapper sources instead
of the owner sources, and optionally validating the generated artifact
manifest.

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

## Current Limits

The split mock protocol does not yet replace the integrated module-wrapper
mock injection path used by legacy module-wrapper registration. Same-module
registration mode currently rejects module-owned mock injection instead of
rewiring it through `inspect-mocks` / `emit-mocks`. That remaining work is
tracked by
[`docs/stories/035_module_mock_split_protocol_registration.md`](stories/035_module_mock_split_protocol_registration.md).

Standalone declaration-only textual registration is not part of this protocol.
It remains a future opt-in mode with stricter source visibility rules, tracked
by
[`docs/stories/036_textual_declaration_only_registration.md`](stories/036_textual_declaration_only_registration.md).

Full non-CMake parity across supported backends is tracked separately by
[`docs/stories/015_non_cmake_full_parity.md`](stories/015_non_cmake_full_parity.md).
