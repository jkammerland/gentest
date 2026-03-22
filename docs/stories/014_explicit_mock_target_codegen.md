# Story: Explicit Mock Target Codegen

## Goal

Replace the current implicit mock-generation path with an explicit mock-target
build step that:

1. takes explicit mock-definition inputs
2. generates a compile-time mock surface for consumers
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

The explicit mock-target design makes the real dependency graph honest:

```text
mock defs files
  ->
mock codegen target
  ->
generated public header + generated public module
  ->
generated impl/object library
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
  MODULE_NAME <name>
  [HEADER_NAME <file>])
```

Required behavior:

- creates an object library target named `<target>`
- runs mock codegen from the explicit defs files
- generates:
  - a public declaration header
  - a public named module
  - implementation source(s) compiled into the object library
- wires the generated files and codegen step as dependencies of `<target>`
- publishes the generated include directory and generated module surface to
  consumers

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

V1 does not support arbitrary behavioral code in defs files.

### Consumer model

Consumers must see the generated compile-time surface and the linked
implementation target.

Non-module consumers:

```cpp
#include "test_mocks.hpp"
```

Module consumers:

```cpp
import test.mocks;
```

Then both link the generated target:

```cmake
target_link_libraries(my_tests PRIVATE test_mocks)
```

The generated public surface must make both of these usable:

- explicit generated aliases such as `test::mocks::ServiceMock`
- raw `gentest::mock<Service>` after the generated surface is visible

Linking alone is not sufficient. The generated declaration surface must be
included/imported explicitly.

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

- declaration header
- declaration module
- compiled implementation objects

The declaration header/module must contain the full generated specialization
declarations needed to make `gentest::mock<T>` usable at compile time.

The object library provides the corresponding implementation bodies.

## Migration Direction

This replaces the current implicit path rather than coexisting with it.

Expected migration work:

1. add `gentest_add_mocks(...)`
2. convert existing mock/module regressions to explicit defs files
3. remove implicit mock discovery from normal test/module scans
4. remove obsolete bootstrap guidance and old mock include plumbing

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
instead of implicit discovery from the test source.

## Notes

- This design is CMake-first, but the internal codegen path should stay reusable
  so another buildsystem frontend can be added later.
- The old `gentest.mock` public module may remain as a low-level building block,
  but it is no longer the full direct-mock story by itself.
