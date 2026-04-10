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
- no full buildsystem parity with the existing textual workflow

## Proposed User Contract

### Tests

If a source file is a named-module testcase source:

- it must be a primary module interface unit
- it must explicitly `import gentest;`
- it may define non-exported testcase functions and fixtures
- it is never rewritten or replaced by `gentest`

Example:

```cpp
export module demo.tests;

import gentest;
import demo.test_mocks;

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
- it must explicitly `import gentest.mock;`
- consumers import that authored module directly
- `gentest` may generate private implementation units, but not a replacement
  public surface

Example:

```cpp
export module demo.test_mocks;

import gentest.mock;
import demo.service;

export namespace demo::mocks {
using ServiceMock = gentest::mock<demo::Service>;
}
```

## Proposed CMake API

Keep the current mixed/textual APIs for legacy support. Add explicit
module-first APIs instead of overloading them further.

### Module testcase codegen

```cmake
gentest_attach_module_codegen(<target>
  OUTPUT_DIR <dir>
  [SOURCES <files...>])
```

Rules:

- `<target>` must already own the testcase module sources
- if `SOURCES` is omitted, discover them from the target's `FILE_SET CXX_MODULES`
- every source must be a primary named-module interface unit
- partition interface units are rejected in V1
- each source must explicitly `import gentest;`
- `gentest_attach_module_codegen()` generates private same-module
  implementation units and adds them to `<target>`
- the original module interface source remains in the target's file sets

### Module mock target

```cmake
gentest_add_module_mocks(<target>
  SOURCES <files...>
  OUTPUT_DIR <dir>)
```

Rules:

- `SOURCES` are user-authored named-module sources only
- at least one source must import `gentest.mock` and act as the public mock
  surface
- no `HEADER_NAME`
- no `MODULE_NAME`
- no textual defs
- no generated aggregate surface module
- consumers import the authored public mock module and link `<target>`

Example:

```cmake
add_library(demo_service)
target_sources(demo_service
  PUBLIC
    FILE_SET demo_service_modules TYPE CXX_MODULES FILES
      ${CMAKE_CURRENT_SOURCE_DIR}/service.cppm)

gentest_add_module_mocks(demo_test_mocks
  SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/test_mocks.cppm
  OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/gentest_mocks")
target_link_libraries(demo_test_mocks PUBLIC demo_service gentest::gentest)

add_executable(demo_tests main.cpp)
target_sources(demo_tests
  PRIVATE
    FILE_SET demo_test_modules TYPE CXX_MODULES FILES
      ${CMAKE_CURRENT_SOURCE_DIR}/cases.cppm)
target_link_libraries(demo_tests PRIVATE
  gentest::gentest
  gentest::gentest_runtime
  demo_test_mocks)

gentest_attach_module_codegen(demo_tests
  OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/gentest_codegen")
```

## Generated File Layout

Generated files are additive and live under the build tree.

### Test registration outputs

For:

```cpp
export module demo.tests;
```

generate:

```text
<output>/registration/demo/tests.gentest.cppm
```

Content shape:

```cpp
module;

#include "gentest/runner.h"
#include "gentest/fixture.h"

module demo.tests;

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

### Mock outputs

For:

```cpp
export module demo.test_mocks;
```

generate:

```text
<output>/mocks/demo/test_mocks.gentest.cppm
```

Content shape:

```cpp
module demo.test_mocks;

import gentest.mock;

namespace gentest::generated {
// generated mock specialization/attachment bodies
}
```

Important property:

- no generated public header
- no generated aggregate module surface
- no `*_mock_registry.hpp` or `*_mock_impl.hpp` in the public contract for
  module mode

## Internal Design Rules

### Registration

- testcase discovery still happens from the authored source
- registration code is emitted into a same-module implementation unit
- `register_cases(std::span<Case>)` remains the runtime handoff point
- wrapper mode and module-implementation mode must be mutually exclusive per
  source

### Mocks

- module mock codegen runs only from explicit mock module targets
- ordinary testcase module sources do not participate in mock discovery
- source-local mock injection and offset-based insertion are removed from the
  module-mode contract
- `GENTEST_NO_AUTO_MOCK_INCLUDE` is not part of module mode

### Scanning and build graph

- authored primary module interface units stay in the user's `FILE_SET CXX_MODULES`
- generated same-module implementation units are attached privately to the
  consuming target
- codegen still scans the authored source, never the generated implementation
  unit

## Validation and Diagnostics

New module-only validation should fail fast on:

- missing `import gentest;` in testcase module sources
- missing `import gentest.mock;` in module mock surfaces
- partition testcase sources passed to `gentest_attach_module_codegen()`
- textual sources passed to `gentest_add_module_mocks()`
- mixed module/textual defs in the module-only API

## Test Migration

### Keep

These remain relevant with little or no semantic change:

- `gentest_module_auto_discovery`
- `gentest_module_multiline_directives`
- `gentest_module_same_line_directives`
- `gentest_module_cpp_source`
- `gentest_module_first_classic_order`
- `gentest_module_name_literal_false_match`
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
- `gentest_explicit_mock_target_surface`
- `gentest_explicit_mock_target_validation`
- `gentest_explicit_mock_target_install_export`
- `gentest_mixed_module_mock_registry`
- `gentest_module_mock_additive_visibility`
- `gentest_module_mock_imported_sibling`
- `gentest_module_mock_multi_imported_sibling`
- `docs/modules.md`
- non-CMake module contract checks for Bazel/Xmake should be updated to expose
  a separate module-only API instead of wrapper-era parity behavior

### Delete

These are wrapper-era behaviors and should disappear under the new contract:

- `gentest_module_registration_without_import`
- `gentest_module_manual_include_whitespace`
- `gentest_module_partial_manual_codegen_includes`
- `gentest_module_partial_manual_codegen_tsan_race_repro`
- `gentest_module_manual_partial_includes`
- `gentest_module_header_unit_import_preamble`

### Replace

Add these instead:

- `gentest_module_registration_same_module_impl`
- `gentest_module_registration_requires_import`
- `gentest_module_mock_surface_authored_public_module`
- `gentest_module_mock_no_generated_public_surface`
- `gentest_module_codegen_rejects_partition_tests`
- `gentest_module_mock_install_export_authored_surface`

## Migration Plan

1. Add the new module-only APIs beside the current ones.
2. Port the `tests/consumer` fixture first.
3. Add the new replacement tests above.
4. Stop documenting wrapper-era bootstrap behavior in `docs/modules.md`.
5. Keep the current wrapper path only as legacy behavior behind
   `gentest_attach_codegen()` and `gentest_add_mocks()`.
6. After the module-only path is stable, decide whether the wrapper path should
   be frozen or gradually retired.

## Bottom Line

The cleanest design for this repo is:

- authored module interfaces stay authored
- `gentest` generates only private same-module implementation units
- module mocks are explicit authored modules, not generated public surfaces
- module mode requires explicit imports and rejects hidden bootstrap tricks

That gives a smaller, more honest contract than the current wrapper-driven
approach, even though it intentionally gives up some existing "magic".
