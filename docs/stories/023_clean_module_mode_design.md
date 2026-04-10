# Story: Clean Module-Only Design

## Goal

Define a cleaner long-term design for named-module support in `gentest` under
these constraints:

1. module mode does not need full parity with the textual/include-based path
2. user-authored module interface units are immutable inputs
3. testcase functions and fixtures do not need `export`
4. mock support is explicit and module-native
5. framework bootstrap details do not leak into user module code

This document is intentionally opinionated. It favors a small, explicit module
contract over preserving every current wrapper-era behavior.

## Non-Goals

- no attempt to keep the "module registration without `import gentest;`" trick
- no source rewriting for module test files
- no module-mode support for textual mock defs
- no generated aggregate mock surface module
- no partition testcase registration in V1
- no private-module-fragment support in module-only mode
- no global-module-fragment support in V1 clean mode
- no textual includes or header-unit dependencies in the V1 clean-module
  contract
- no full buildsystem parity with the existing textual workflow

## Proposed User Contract

### Tests

If a source file is a named-module testcase source:

- it must be a primary module interface unit
- it must not use a global module fragment
- it must not contain `module :private;`
- it must explicitly `import gentest;` itself
- testcase functions and fixtures must not be exported
- it is never rewritten or replaced by `gentest`

Example:

```cpp
export module demo.tests;

import gentest;
import demo.test_mocks;
import demo.service;

using namespace gentest::asserts;

namespace demo {
[[using gentest: test("demo/module_mock")]]
void module_mock() {
    mocks::ServiceMock mock;
    gentest::expect(mock, &Service::compute).times(1).with(3).returns(9);
    Service *service = &mock;
    EXPECT_EQ(service->compute(3), 9);
}
} // namespace demo
```

### Mocks

If a source file is a public mock surface:

- it must be a user-authored named module
- it must be a primary module interface unit
- it must not use a global module fragment
- it must not contain `module :private;`
- it must explicitly `import gentest.mock;`
- consumers import that authored module directly
- module mode guarantees authored aliases such as `demo::mocks::ServiceMock`,
  not raw `gentest::mock<T>` as a downstream consumer contract
- if tests name domain types directly, they import the domain modules directly
- if the authored public mock surface mentions entities from another module in
  exported declarations, it must `export import` that module explicitly
- `gentest` may generate private implementation units, but not a replacement
  public surface
- a module-mock target exposes exactly one authored public mock surface in V1

Example:

```cpp
export module demo.test_mocks;

export import gentest.mock;
export import demo.service;

export namespace demo::mocks {
using ServiceMock = gentest::mock<demo::Service>;
}
```

## Proposed CMake API

Keep the current mixed/textual APIs for legacy support. Add explicit
module-first APIs instead of overloading them further.

### Module testcase registration

```cmake
gentest_register_module_tests(<target>
  FILE_SET <name>)
```

Rules:

- `<target>` must already exist
- `<target>` must already be fully populated with its module sources,
  dependencies, and support/provider links before calling the gentest API
- `<target>` must already link `gentest::gentest` and either
  `gentest::gentest_runtime` or `gentest::gentest_main`
- `FILE_SET <name>` is required in V1
- the named file set must already belong to `<target>`
- the named file set must contain only testcase module sources
- every source in the named file set must be a primary named-module interface
  unit
- partition interface units are rejected in V1
- sources using a global module fragment are rejected in V1
- sources containing `module :private;` are rejected in V1
- each source must explicitly `import gentest;`
- `gentest_register_module_tests()` generates private same-module
  implementation units and adds them to `<target>`
- the original module interface source remains in the target's file sets

### Module mock target

```cmake
gentest_enable_module_mocks(<target>
  FILE_SET <name>)
```

Rules:

- `<target>` must already exist
- `<target>` must already be fully populated with its module sources and normal
  dependencies before calling the gentest API
- `<target>` must be a linkable library target intended to publish a public
  authored mock surface
- `FILE_SET <name>` is required in V1
- the named file set must already belong to `<target>`
- the named file set must contain exactly one authored public mock surface in V1
- the named file set must be the selected `PUBLIC` `CXX_MODULES` file set that
  defines the module-mode package contract for the target
- that authored mock surface must be a primary named-module interface unit
- that authored mock surface must not use a global module fragment
- that authored mock surface must not contain `module :private;`
- that authored mock surface must explicitly `import gentest.mock;`
- no generated aggregate surface module
- no `HEADER_NAME`
- no `MODULE_NAME`
- no textual defs
- consumers import the authored public mock module and link `<target>`
- support/provider modules are attached through ordinary `target_sources()` or
  normal target dependencies and are resolved from the final target graph, not
  from extra gentest-specific input lists
- dependencies named in exported mock-surface declarations must be `PUBLIC`
  and explicitly reachable through that authored surface; implementation-only
  dependencies may remain `PRIVATE`

Example:

```cmake
add_library(demo_service)
target_sources(demo_service
  PUBLIC
    FILE_SET demo_service_modules TYPE CXX_MODULES FILES
      ${CMAKE_CURRENT_SOURCE_DIR}/service.cppm)

add_library(demo_test_mocks)
target_sources(demo_test_mocks
  PUBLIC
    FILE_SET mock_surface TYPE CXX_MODULES FILES
      ${CMAKE_CURRENT_SOURCE_DIR}/test_mocks.cppm)
target_link_libraries(demo_test_mocks PUBLIC demo_service gentest::gentest)
gentest_enable_module_mocks(demo_test_mocks
  FILE_SET mock_surface)

add_executable(demo_tests main.cpp)
target_sources(demo_tests
  PRIVATE
    FILE_SET demo_test_modules TYPE CXX_MODULES FILES
      ${CMAKE_CURRENT_SOURCE_DIR}/cases.cppm)
target_link_libraries(demo_tests PRIVATE
  gentest::gentest
  gentest::gentest_runtime
  demo_test_mocks)

gentest_register_module_tests(demo_tests
  FILE_SET demo_test_modules)
```

### Install and export contract

The authored mock surface is the public package contract for module-mode mocks.

- `gentest_enable_module_mocks()` uses the existing authored public mock
  surface `FILE_SET CXX_MODULES` as the package contract
- generated same-module implementation units are plain `PRIVATE` sources with
  module scanning enabled
- `export()` / `install(EXPORT ...)` for such targets must preserve standard
  CMake C++ module metadata for the authored public surface
- until imported CMake module metadata alone is sufficient for downstream
  codegen, enabled module-mock targets also export a gentest-specific
  module-name-to-relative-authored-source mapping, relative to the installed
  public module file-set base directory, so downstream
  `gentest_codegen` can reconstruct its `--external-module-source=...`
  arguments for imported mock targets
- downstream `gentest_codegen` resolves imported enabled-mock targets by
  walking linked imported targets, reading both standard CMake module metadata
  and the exported gentest-specific authored-surface map, and reconstructing
  the transitive imported mock/provider module source mappings needed to parse
  testcase modules
- downstream `find_package()` consumers import the authored module from the
  installed target; they do not import a generated aggregate or compatibility
  module
- downstream consumers also import any domain modules they name directly; the
  mock surface is not required to re-export them
- rebuild/invalidation of generated mock implementation units must track both
  edits to the authored public mock surface and edits to support/provider
  dependencies reachable through the final target graph

### Generator and output constraints

V1 clean-module mode keeps the current conservative build constraints unless
proven otherwise:

- single-config generators only
- Ninja is the expected V1 generator
- generated implementation-unit output paths remain concrete and unique per
  target inside the build tree
- any debug/testing override for output location is not part of the primary
  public API

## Generated File Layout

Generated files are additive and live under the build tree.

### Test registration outputs

For:

```cpp
export module demo.tests;
```

generate:

```text
<binary>/gentest/<target>/registration/demo/tests.gentest.cpp
```

Content shape:

```cpp
module demo.tests;

import gentest;

namespace {
static void wrap_0(void*) { demo::module_mock(); }

constexpr std::array<gentest::Case, 1> kCases = {{
    /* ... */
}};

struct Registrar {
    Registrar() {
        gentest::detail::register_cases(std::span{kCases});
    }
};

inline const Registrar kRegistrar{};
} // namespace
```

Important property:

- no generated `tu_*.module.gentest.cppm`
- no registration header included back into the authored module source
- generated same-module implementation units are ordinary `PRIVATE` sources and
  must never be added to any `FILE_SET CXX_MODULES`

### Mock outputs

For:

```cpp
export module demo.test_mocks;
```

generate:

```text
<binary>/gentest/<target>/mocks/demo/test_mocks.gentest.cpp
```

Content shape:

```cpp
module demo.test_mocks;

import gentest.mock;
import demo.service;

namespace gentest::generated {
// generated mock specialization/attachment bodies
}
```

Important property:

- no generated public header
- no generated aggregate module surface
- no `*_mock_registry.hpp` or `*_mock_impl.hpp` in the public contract for
  module mode
- generated same-module implementation units are ordinary `PRIVATE` sources and
  must never be added to any `FILE_SET CXX_MODULES`

## Internal Design Rules

### Registration

- testcase discovery still happens from the authored source
- registration code is emitted into a same-module implementation unit
- `register_cases(std::span<Case>)` remains the runtime handoff point
- wrapper mode and module-implementation mode must be mutually exclusive per
  source
- module-only registration is for primary multi-unit named modules only

### Mocks

- module mock codegen runs only from explicit mock module targets
- ordinary testcase module sources do not participate in mock discovery
- source-local mock injection and offset-based insertion are removed from the
  module-mode contract
- `GENTEST_NO_AUTO_MOCK_INCLUDE` is not part of module mode
- enabled module-mock targets derive support/provider visibility from the final
  target graph at the time the gentest API is called
- generated same-module mock implementation units import the selected authored
  mock surface and any provider/domain modules required by generated bodies, as
  determined from the final target graph and codegen discovery

### Scanning and build graph

- authored primary module interface units stay in the user's `FILE_SET CXX_MODULES`
- generated same-module implementation units are attached privately to the
  consuming target as ordinary `PRIVATE` sources with module scanning enabled
- codegen still scans the authored source, never the generated implementation
  unit
- module-only APIs do not infer inputs from every `CXX_MODULE_SET_*`; callers
  provide a specific file-set name explicitly

## Validation and Diagnostics

New module-only validation should fail fast on:

- missing `import gentest;` in testcase module sources
- missing `import gentest.mock;` in module mock surfaces
- testcase or mock sources using a global module fragment
- partition testcase sources passed to `gentest_register_module_tests()`
- authored module sources containing `module :private;`
- non-interface or textual sources selected by `gentest_register_module_tests()`
- non-interface or textual sources selected by `gentest_enable_module_mocks()`
- unknown or unowned file sets passed to the clean module APIs
- use of both legacy wrapper mode and module-only mode for the same source or
  target
- module-mock targets exposing anything other than exactly one authored public
  mock surface

## Test Migration

### Keep

These remain relevant with little or no semantic change:

- `gentest_module_multiline_directives`
- `gentest_module_same_line_directives`
- `gentest_module_cpp_source`
- `gentest_module_pcm_cache_isolation`
- `gentest_public_module_old_ninja_fallback`
- `gentest_public_module_missing_scan_deps_fallback`
- `gentest_clang_scan_deps_program_resolution`
- `gentest_module_fixture_compiler_resolution`
- `gentest_public_mock_module_gcc`

### Rewrite

These should survive, but their expectations need to switch from wrapper files
and generated aggregate surfaces to authored-module-plus-generated-impl-unit
behavior:

- `tests/consumer/CMakeLists.txt`
- `tests/consumer/cases.cppm`
- `gentest_mixed_module_mock_registry`
- `gentest_module_mock_additive_visibility`
- `gentest_module_mock_imported_sibling`
- `gentest_module_mock_multi_imported_sibling`
- `docs/modules.md`
- `gentest_module_doc_contract`
- `gentest_non_cmake_module_contract`
- package-consumer tests driven by `tests/consumer/CMakeLists.txt`
- non-CMake module contract checks for Bazel/Xmake should be updated to expose
  a separate module-only API instead of wrapper-era parity behavior

### Split

These currently mix legacy textual assertions with module assertions and should
be split before porting:

- `gentest_explicit_mock_target_validation`
- `gentest_explicit_mock_target_surface`
- `gentest_explicit_mock_target_install_export`
- `gentest_module_name_literal_false_match`
- `gentest_module_partial_manual_codegen_includes`
- `gentest_module_partial_manual_codegen_tsan_race_repro`

Until that split lands, the existing `gentest_explicit_mock_target_*` entries
in [tests/CMakeLists.txt](/home/ai-dev1/repos/story-branch-coverage-worst-file/tests/CMakeLists.txt)
remain legacy coverage only. They are not clean-module acceptance tests.

### Delete

These are wrapper-era behaviors and should disappear under the new contract:

- `gentest_module_registration_without_import`
- `gentest_module_manual_include_whitespace`
- `gentest_module_manual_partial_includes`
- `gentest_module_header_unit_import_preamble`
- `gentest_module_partition_registration_visibility`

### Legacy / Defer

These remain legacy-path coverage and do not belong to the initial clean-module
acceptance matrix:

- `gentest_module_first_classic_order`
- `gentest_explicit_mock_target_staging`

### Replace

Add these instead:

- `gentest_module_registration_same_module_impl`
- `gentest_module_registration_requires_import`
- `gentest_module_registration_requires_explicit_file_set`
- `gentest_module_registration_rejects_private_fragment`
- `gentest_module_registration_rejects_global_fragment`
- `gentest_module_mock_surface_authored_public_module`
- `gentest_module_mock_no_generated_public_surface`
- `gentest_module_codegen_rejects_partition_tests`
- `gentest_module_mocks_reject_missing_gentest_mock_import`
- `gentest_module_mocks_reject_global_fragment`
- `gentest_module_mocks_reject_private_fragment`
- `gentest_module_mocks_reject_textual_surface`
- `gentest_module_mocks_reject_non_interface_source`
- `gentest_module_mocks_reject_unowned_file_set`
- `gentest_module_mocks_require_public_file_set`
- `gentest_module_mocks_enforce_single_surface`
- `gentest_module_registration_rejects_unowned_file_set`
- `gentest_module_registration_rejects_non_interface_source`
- `gentest_module_legacy_clean_mutual_exclusion`
- `gentest_module_registration_layout_no_wrapper_outputs`
- `gentest_module_registration_end_to_end_fixture_bench_jitter`
- `gentest_module_mock_install_export_authored_surface`
- `gentest_module_mock_rebuild_propagation`
- `gentest_module_mock_support_dependency_rebuild`
- `gentest_module_parallel_parse_policy`
- `gentest_module_parallel_parse_tsan_repro`
- `gentest_module_register_requires_fully_populated_target`
- `gentest_module_mocks_require_fully_populated_target`
- `gentest_module_mock_install_export_downstream_codegen`

## Migration Plan

1. Add the new module-only APIs beside the current ones.
2. Prove them with parallel clean fixtures instead of rewriting
   `tests/consumer` immediately.
3. Add the new replacement tests above against those clean fixtures.
4. Split mixed wrapper/module tests before changing semantics.
   This includes keeping the existing `gentest_explicit_mock_target_*`
   registrations in [tests/CMakeLists.txt](/home/ai-dev1/repos/story-branch-coverage-worst-file/tests/CMakeLists.txt)
   as legacy coverage until their clean-module successors exist.
5. Preserve generic parallel-parse and TSAN coverage under new dedicated tests.
6. Rewrite `docs/modules.md` and the module doc-contract tests after the new
   CMake path is stable.
7. Port `tests/consumer`, package-consumer coverage, and non-CMake backends
   after that.
8. Keep the current wrapper path only as legacy behavior behind
   `gentest_attach_codegen()` and `gentest_add_mocks()`.
9. After the module-only path is stable, decide whether the wrapper path should
   be frozen or gradually retired.

## Bottom Line

The cleanest design for this repo is:

- authored module interfaces stay authored
- `gentest` generates only private same-module implementation units
- module mocks are explicit authored modules, not generated public surfaces
- module mode requires explicit imports and rejects hidden bootstrap tricks
- installed module-mode mock targets export the authored public module as the
  package contract

That gives a smaller, more honest contract than the current wrapper-driven
approach, even though it intentionally gives up some existing "magic".
