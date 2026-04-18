# Story: optional declaration-only textual registration

## Status

Open.

## Goal

Add an opt-in textual registration mode that emits standalone generated
registration sources for eligible `.cpp` tests without including the owning
source file. The existing textual wrapper mode remains the default because it is
the only compatibility-preserving path for anonymous namespaces, `static`
tests, source-local fixtures, and other `.cpp`-local declarations.

## Problem

Story `034` intentionally keeps textual registration on wrapper/include mode:

```cpp
#include "../tests/cases.cpp"
#include "tu_0000_cases.gentest.h"
```

That preserves current semantics, but some projects may prefer declaration-only
generated registration units when all called tests and fixture types are visible
from another translation unit. That mode has stricter source-style rules and
needs explicit diagnostics instead of becoming an accidental default.

## Scope

In scope:

- add an explicit opt-in CLI/build-system switch for declaration-only textual
  registration
- define manifest entries for declaration-only textual outputs, including
  `includes_owner_source: false` and `replaces_owner_source: false`
- generate standalone registration sources for eligible textual tests
- reject or clearly diagnose source-local tests, anonymous-namespace tests,
  `static` tests, and fixture/signature types that are not visible to the
  generated translation unit
- keep wrapper/include mode as the default path for ordinary textual sources

Out of scope:

- replacing wrapper mode as the default
- supporting `.cpp`-local declarations in declaration-only mode
- same-module registration
- full non-CMake parity

## Example

Eligible authored source:

```cpp
#include "cases.hpp"

namespace textual_tests {

[[using gentest: test("textual/public_case")]]
void public_case() {}

} // namespace textual_tests
```

Generated standalone registration source:

```cpp
#include "cases.hpp"
#include "gentest/runner.h"

namespace gentest::generated::tu_0000 {

static void public_case_wrapper(void*) {
    textual_tests::public_case();
}

// generated case table and registrar

} // namespace gentest::generated::tu_0000
```

Rejected source shape:

```cpp
namespace {

[[using gentest: test("textual/local")]]
void local_case() {}

} // namespace
```

## Acceptance Criteria

- Default textual `gentest_attach_codegen(...)` behavior continues to emit and
  compile wrapper/include artifacts.
- Opt-in declaration-only mode emits standalone registration artifacts whose
  manifest records that the owner source is not included or replaced.
- A positive regression compiles and runs a declaration-only textual case with
  externally visible declarations.
- Negative regressions reject anonymous-namespace, `static`, and source-local
  fixture/type cases with actionable `gentest_codegen` diagnostics.
- CMake remains a thin adapter; semantic eligibility checks live in
  `gentest_codegen`.

