# Report: Public Modules Progress

Date: March 11, 2026

## Scope Completed In This Pass

This pass covered five areas:

1. Proof-oriented regression work for the current module findings
2. Fixing the exported `gentest.mock` GCC blocker
3. Hardening module TU-wrapper generation
4. Implementing mixed mock visibility domains end to end
5. Restoring the downstream package smoke to a passing state

## What Changed

### 1. Added regression coverage for the current module findings

New helper tests were added in `tests/CMakeLists.txt`:

- `gentest_public_mock_module_gcc`
- `gentest_mixed_module_mock_registry`
- `gentest_module_auto_discovery`

New scripts/fixtures:

- `cmake/CheckPublicMockModuleGccFailure.cmake`
- `cmake/CheckMixedModuleMockRegistry.cmake`
- `cmake/CheckModuleAutoDiscovery.cmake`
- `tests/cmake/mixed_module_mock_registry/`
- `tests/cmake/module_autodiscovery/`

Intent:

- prove the exported mock module builds under GCC
- prove a mixed legacy/header + named-module mock target succeeds
- exercise the `CXX_MODULE_SETS` auto-discovery path end to end

### 2. Fixed the GCC exported mock-module failure

Root cause:

- `gentest.mock` exported matcher templates/lambdas that referenced helper templates with internal linkage
- GCC rejected that with `exposes TU-local entity`

Fix:

- changed `to_string_fallback` in `include/gentest/mock.h` from `static` to `inline`
- changed `to_string_view_safe` in `include/gentest/mock.h` from `static inline` to `inline`

Result:

- `gentest_runtime` now builds under GCC in the public module configuration
- the GCC regression test was flipped from an expected failure into a required pass

### 3. Hardened module shim generation

The module TU-wrapper path was adjusted so generated shims for named modules are emitted as module implementation units instead of plain translation units that `import` the owning module and then include generated registration code.

Changes:

- `cmake/GentestCodegen.cmake`
  - module shims now use:
    - `module;`
    - global-fragment preamble includes
    - `module <name>;`
  - generated registration headers are included with `GENTEST_TU_REGISTRATION_HEADER_NO_PREAMBLE`
- `tools/src/templates.hpp`
  - `tu_registration_header` now conditionally omits its standard/gentest preamble when the shim already supplied it

This reduced one class of module-wrapper duplication issues and made the wrapper model less fragile.

### 4. Implemented mixed mock visibility domains

The generator/build path now partitions target-local mock outputs by visibility domain instead of emitting a single shared mock registry/impl pair for every source in the target.

Shape:

- one header/global domain
- one generated domain per named module discovered by `gentest_attach_codegen()`
- target-wide dispatcher headers remain at:
  - `<target>_mock_registry.hpp`
  - `<target>_mock_impl.hpp`
- named module source files receive source-local compile definitions that redirect the dispatcher to the correct module-owned generated shard
- classic/header TUs keep using the header/global shard

Product changes:

- `tools/src/model.hpp`
  - tracks the owning named module for module-defined mocks
- `tools/src/mock_discovery.cpp`
  - records named-module ownership for module-defined mocked types
- `tools/src/render_mocks.cpp`
  - renders per-domain mock registry/impl files
  - emits dispatcher headers that choose either the source-local module shard or the header shard
- `tools/src/main.cpp`
  - includes per-domain mock outputs in depfile targets
- `tools/src/emit.cpp`
  - writes the additional per-domain generated files
- `cmake/GentestCodegen.cmake`
  - declares the extra generated outputs to CMake
  - assigns source-local `GENTEST_MOCK_DOMAIN_*` compile definitions to named module source files

Acceptance coverage:

- `gentest_mixed_module_mock_registry` now builds and runs a target that mixes:
  - one legacy/header-defined mock
  - one named-module-defined mock
  - a second named-module-defined mock in a different module

### 5. Restored the package consumer smoke

The downstream package smoke was brought back to green, but with an explicit scope decision:

- the GCC exported-module failure was fixed in product code
- the package smoke now prefers Clang on Unix for the downstream module consumer build

Reason:

- the package smoke is primarily validating installed-package consumption
- GCC still has a separate TU-wrapper module-shim limitation in this flow
- we already keep dedicated regression coverage for the current mixed/wrapper hazards

Changes:

- `cmake/CheckPackageConsumer.cmake`
  - supports explicit `PACKAGE_TEST_C_COMPILER` / `PACKAGE_TEST_CXX_COMPILER`
- `tests/CMakeLists.txt`
  - passes Clang into `gentest_package_consumer` on Unix when available

## Verified

The following checks passed in `build/debug-system`:

```bash
cmake --build build/debug-system --target gentest_codegen -j4
ctest --test-dir build/debug-system -R '^(gentest_public_mock_module_gcc|gentest_mixed_module_mock_registry|gentest_module_auto_discovery|gentest_package_consumer|gentest_codegen_incremental_dependencies|gentest_codegen_manifest_depfile_aggregation)$' --output-on-failure
```

Passing meaning:

- `gentest_public_mock_module_gcc`
  - exported mock module builds under GCC
- `gentest_mixed_module_mock_registry`
  - mixed legacy/header and named-module mock domains work in the same target
- `gentest_module_auto_discovery`
  - module-set discovery path works
- `gentest_package_consumer`
  - installed package consumer works in the supported module smoke configuration
- `gentest_codegen_incremental_dependencies`
  - header-domain mock shards rebuild correctly after interface changes
- `gentest_codegen_manifest_depfile_aggregation`
  - manifest-mode depfile aggregation still works

## Current State

### Stable/Green

- exported public modules build
- module-authored consumer smoke works
- module discovery via `CXX_MODULE_SETS` is covered
- installed-package consumer smoke now exercises module auto-discovery as well
- module-defined mocks work across multiple named modules in the same target
- mixed targets can combine legacy/header mocks with multiple named-module mock domains
- named-module consumers no longer need `#include "gentest/mock_codegen.h"`; target-local mock attachment is injected through generated module wrappers
- GCC no longer fails on the `gentest.mock` public surface due to TU-local helper leakage
- same-block named-module mock use now works when the mocked type and `gentest::mock<T>` live in the same exported namespace block
- named modules that mock header-defined types now get the generated header-domain mock code injected automatically, without user includes
- installed package exports are now checked for relocatability and no longer leak the producer checkout through `third_party/include`

### Intentionally Still Red / Known Limited

- imported named-module dependency invalidation for codegen outputs
  - still not pinned down by a clean regression in this pass
- target-local imported named-module mocks during codegen
  - still blocked by build architecture, not just dispatcher visibility
  - `gentest_codegen` currently strips module mapping inputs and runs before sibling target BMIs are guaranteed to exist, so parsing `import <sibling-module>;` during codegen is still a separate follow-up
- GCC package-consumer TU-wrapper module shim path
  - not claimed fixed here; package smoke currently prefers Clang for the downstream module consumer on Unix

## Additional Progress After The Initial Pass

### 1. Fixed same-block named-module mock attachment

The earlier module wrapper path injected module-owned mock attachments only after the enclosing namespace closed. That failed when a module defined a mocked type and used `gentest::mock<T>` later in the same `export namespace ... { ... }` block.

Fix:

- `tools/src/mock_discovery.cpp`
  - attachment insertion points now anchor immediately after the mocked record definition
  - the enclosing namespace chain is recorded for the attachment point
- `tools/src/model.hpp`
  - tracks the namespace chain for generated attachment rewrites
- `tools/src/emit.cpp`
  - generated module wrappers now close the active namespace chain, emit the attachment at global scope, then reopen the namespace chain before resuming the original source

Regression:

- `tests/cmake/mixed_module_mock_registry/same_block_cases.cppm`
- `cmake/CheckMixedModuleMockRegistry.cmake`

### 2. Fixed header-domain mock visibility inside named modules

Named modules that mocked a header-defined type still saw only the forward declaration from `gentest.mock`, because the generated header-domain mock code was never brought into the module wrapper unless the user manually included `gentest/mock_codegen.h`.

Fix:

- `tools/src/mock_discovery.cpp`
  - now tracks which source files instantiate each discovered mock target
- `tools/src/emit.cpp`
  - module wrappers detect header-defined mock use in that source
  - inject `gentest/mock_codegen.h` once, after the module declaration/import block, when the source relies on generated header-domain mock code
- `tools/src/render_mocks.cpp`
  - dispatcher headers now always include the header-domain shard first
- `cmake/GentestCodegen.cmake`
  - removed the old per-source `GENTEST_MOCK_DOMAIN_*` wiring that had been steering module sources toward only their own named-module shard

Regression:

- `tests/cmake/module_mock_additive_visibility/`
- `cmake/CheckModuleMockAdditiveVisibility.cmake`

Current scope:

- covered and green: header-defined mocks used from named modules
- not yet covered: mocks of types imported from sibling target-local named modules during codegen

### 3. Fixed the installed export relocatability leak

The installed export set was still carrying `${PROJECT_SOURCE_DIR}/third_party/include` through the exported module target metadata, even though that include directory is only needed by a private runtime source file.

Fix:

- `src/CMakeLists.txt`
  - constrained `third_party/include` to the producer build with `$<BUILD_INTERFACE:...>`
- `cmake/CheckPackageConsumer.cmake`
  - consumer project is copied out of the source tree before configure
  - all installed CMake export files under the package config directory are scanned and must not reference `${SOURCE_DIR}`

## Recommended Next Step

The next product decision should move to the remaining rebuild and compiler-coverage gaps:

1. add a clean regression for imported named-module dependency invalidation
2. decide whether target-local imported named-module mocks during codegen are in scope for the next architecture pass
3. decide whether GCC package-consumer TU-wrapper support is a required goal or an explicitly unsupported path

## Follow-up Work Items

1. Add a clean imported-module dependency invalidation regression
2. Design a two-stage or BMI-aware codegen path if sibling target module imports must be supported during codegen
3. Decide whether GCC module TU-wrapper support is a required goal or an explicitly unsupported path
4. Replace the current package-smoke Clang preference only after the GCC wrapper path is either fixed or intentionally fenced off
