# Story: Explicit Mock Target Codegen

## Goal

Replace the current implicit mock-generation path with an explicit mock-target
build step that:

1. takes explicit mock-definition inputs
2. generates a compile-time mock surface for consumers that preserves the
   authored surface model
3. generates compiled mock implementation objects
4. removes ordinary test/module sources from the mock-discovery path

This is the selected follow-up to
[`013_module_mock_bootstrap_options.md`](013_module_mock_bootstrap_options.md).

## Why This Direction

The current module-mock path is a hybrid:

- public module API comes from `import gentest.mock;`
- target-local mock specializations still come from generated code
- module wrappers inject attachment code after discovery
- module-authored mocks still expose generator bootstrap concerns

That architecture works, but it is not explicit and it is not beautiful.

The explicit mock-target design makes the real dependency graph honest.

It should also preserve the author's chosen surface model:

- textual defs stay textual
- module defs stay modular
- the system should not silently invent the opposite surface by default

That means the primary design is not:

```text
mock defs files
  ->
mock codegen target
  ->
generated public surface matching the defs style
  ->
generated impl/object target
  ->
consumer includes/imports the generated surface and links the target
```

## Chosen Model

### Buildsystem surface

First implementation is CMake-first.

Add a new CMake API:

```cmake
gentest_add_mocks(<target>
  DEFS <files...>
  OUTPUT_DIR <dir>
  [MODULE_NAME <name>]
  [HEADER_NAME <file>])
```

Required behavior:

- creates a generated mock target named `<target>`
- runs mock codegen from the explicit defs files
- generates implementation source(s) compiled into the target
- generates the public compile-time surface that matches the authored defs:
  - textual defs:
    - public declaration header
  - module defs:
    - public named module
- wires the generated files and codegen step as dependencies of `<target>`
- publishes only the generated surface that actually belongs to that target

Default rule:

- textual defs do not implicitly become a named module
- module defs do not implicitly become a generated compatibility header

Out of scope for the primary design:

- auto-generating a named module from header-like defs
- auto-generating a classic header bridge from module defs

If we later want bridge surfaces, they should be separate, explicit features.

### Input model

V1 defs files are alias-declaration files only.

Allowed contents:

- includes/imports needed to make mocked types visible
- namespaces
- alias/type declarations such as:

```cpp
// mock_defs_1.hpp
#include <gentest/mock.h>

namespace test::mocks {
using ServiceMock = gentest::mock<Service>;
}
```

Supported file kinds:

- `.hpp`
- `.cpp`
- `.cppm`

For module defs, the file should behave like an ordinary named module unit and
explicitly `import gentest.mock;` before declaring aliases based on
`gentest::mock<T>`.

V1 does not support arbitrary behavioral code in defs files.

Surface rule:

- textual defs:
  - may use includes
  - produce a textual public mock surface
- module defs:
  - must declare real named modules
  - produce a modular public mock surface

If a user wants a mock module, they should author module defs for it. We do not
infer a named module from plain headers.

### Consumer model

Consumers must see the generated compile-time surface and the linked
implementation target.

Textual defs consumers:

```cpp
#include "test_mocks.hpp"
```

Module defs consumers:

```cpp
import test.mocks;
```

Then both link the generated target:

```cmake
target_link_libraries(my_tests PRIVATE test_mocks)
```

The current contract is: link explicit mock targets before
`gentest_attach_codegen()`, so the generated mock surface is already visible
during codegen.

The generated public surface must make both of these usable within that surface
model:

- explicit generated aliases such as `test::mocks::ServiceMock`
- raw `gentest::mock<Service>` after the generated surface is visible

Linking alone is not sufficient. The generated declaration surface must be
included/imported explicitly.

Important implication:

- a module-defined mocked type is expected to be consumed through the generated
  module surface
- a header-defined mocked type is expected to be consumed through the generated
  header surface

Cross-surface bridging is not part of the primary contract.

## Internal Implementation Direction

### Reuse

Reuse the current infrastructure where it still fits:

- mock discovery from parsed source
- generated specialization rendering
- `MockAccess`/dispatch/runtime machinery
- compiled mock implementation emission

### Remove from ordinary test flow

Remove implicit mock generation from ordinary test/module sources.

That means:

- `gentest_attach_codegen()` no longer discovers mocks from regular test files
- the old `GENTEST_MOCK_*`-driven include path is no longer the primary mock
  system
- direct module mocks no longer rely on source-level bootstrap patterns as the
  primary supported path

The explicit defs files become the only source of truth for generated mocks.

### Output shape

The generated mock target owns:

- the declaration surface matching the defs style
- compiled implementation objects

That declaration surface must contain the full generated specialization
declarations needed to make `gentest::mock<T>` usable at compile time within
its own consumption model.

The object library provides the corresponding implementation bodies.

## Migration Direction

This replaces the current implicit path rather than coexisting with it.

Expected migration work:

1. add `gentest_add_mocks(...)`
2. convert existing mock/module regressions to explicit defs files
3. preserve the authored surface model instead of synthesizing the opposite one
4. remove implicit mock discovery from normal test/module scans
5. remove obsolete bootstrap guidance and old mock include plumbing

## Test Plan

Minimum regression set:

- explicit mock target from header defs file
- explicit mock target from `.cppm` defs file
- header consumer using explicit alias
- header consumer using raw `gentest::mock<T>`
- module consumer using explicit alias
- module consumer using raw `gentest::mock<T>`
- mocked type defined in a header
- mocked type defined in an imported named module
- multi-mock / multi-namespace defs file
- downstream/package-style consumer using the generated surface
- rebuild/sync behavior when defs files change

Existing mock/module regressions should be reworked to use explicit defs inputs
instead of implicit discovery from the test source, but they should still
follow the same surface rule:

- header defs regressions should validate header consumers
- module defs regressions should validate module consumers

## Notes

- This design is CMake-first, but the internal codegen path should stay reusable
  so another buildsystem frontend can be added later.
- The old `gentest.mock` public module may remain as a low-level building block,
  but it is no longer the full direct-mock story by itself.
- If we later want module-to-header or header-to-module bridge surfaces, those
  should be explicit follow-on stories, not the default contract of
  `gentest_add_mocks()`.
