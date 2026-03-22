# Story: Module Mock Bootstrap Options

> [!NOTE]
> Decision update: Option 2 was chosen as the long-term direction. Follow-on
> work is tracked in
> [`014_explicit_mock_target_codegen.md`](014_explicit_mock_target_codegen.md).

## Goal

Remove the user-visible `#if defined(GENTEST_CODEGEN)` bootstrap pattern from
module-authored test sources, while preserving the current named-module test and
mock workflow.

The concrete user-facing problem is that the current module mock examples still
need a codegen-only preprocessor block such as:

```cpp
module;

#if defined(GENTEST_CODEGEN)
#define GENTEST_NO_AUTO_MOCK_INCLUDE 1
#include "gentest/mock.h"
#undef GENTEST_NO_AUTO_MOCK_INCLUDE
#endif

export module my.tests;

import gentest.mock;
```

That is implementation debt. It leaks the generator bootstrap path into normal
module source code.

## Problem Statement

At first glance, `import gentest.mock;` looks like it should be enough:

1. named modules should replace textual include hacks
2. exported templates should remain usable after import
3. the final executable build happens after codegen has emitted all generated
   files

Those statements are true in general, but they do not fully match the current
`gentest::mock<T>` implementation.

The public `gentest.mock` module exports the public mock API surface. The real,
target-local mock implementation for a mocked type is still generated later by
`gentest_codegen` from the consuming target's sources.

That means the first codegen parse and the final compile do not see the same
world.

## Current Pipeline

Today the pipeline is:

```text
user source (.cppm)
  |
  | first parse for gentest_codegen
  | GENTEST_CODEGEN=1
  | source may expose codegen-only mock.h support
  v
gentest_codegen parses original module source
  |
  | discovers tests, fixtures, mocks
  | emits registration wrappers and generated mock code
  v
generated wrapper source / generated mock files
  |
  | normal compile
  v
compiler builds final module / executable
```

For direct module mocks, the first parse happens before the generated
`mock<Service>` specialization and attachment code exist. The current workaround
is that `gentest/mock.h` has a `GENTEST_CODEGEN` placeholder mode that keeps the
source parseable until the real generated code is available.

Relevant implementation pieces:

- `include/gentest/mock.h`
- `include/gentest/gentest.mock.cppm`
- `tools/src/main.cpp`
- `tools/src/emit.cpp`
- `tools/src/render_mocks.cpp`

## Why `import gentest.mock;` Is Not Enough Today

`import gentest.mock;` imports the public module BMI. That BMI cannot already
contain the consuming target's future generated `mock<T>` specialization for
types defined in that target.

Conceptually:

```text
import gentest.mock;
  -> public mock API surface
  -> public matcher helpers
  -> public expect()/make_nice()/make_strict()

NOT yet available:
  -> target-local generated mock<Service>
  -> target-local generated MockAccess<mock<Service>>
  -> target-local generated method bodies / verification plumbing
```

So the tension is:

```text
first codegen parse needs a parseable mock<T>
but
the real target-local mock<T> implementation is only generated after that parse
```

This is why the current source-level bootstrap block exists.

## Comparison: Three Pipelines

### 1. Current pipeline

```text
source contains codegen bootstrap block
        |
        v
gentest_codegen parses source in bootstrap mode
        |
        v
gentest_codegen emits target-local generated mock code
        |
        v
final compile sees generated wrapper + generated mock implementation
```

Pros:

- already implemented
- preserves current generated-mock architecture

Cons:

- leaks generator mechanics into user code
- poor front-facing module story
- makes examples look less “module-native” than they should

### 2. Option 1: generator-synthesized bootstrap source

Keep the current generated mock architecture, but move the bootstrap hack out of
user sources and into `gentest_codegen`.

```text
clean user source (.cppm)
        |
        v
gentest_codegen reads original source
        |
        | synthesizes a parse-only temporary source or in-memory preamble
        | with the bootstrap includes it needs
        v
gentest_codegen parses synthetic bootstrap source
        |
        v
gentest_codegen emits target-local generated mock code
        |
        v
final compile sees generated wrapper + generated mock implementation
```

Pros:

- removes the user-visible `#if defined(GENTEST_CODEGEN)` block
- preserves the current module mock implementation strategy
- smallest realistic fix

Cons:

- bootstrap complexity still exists internally
- codegen must detect when bootstrap includes are needed
- synthetic-source behavior must stay aligned with the final wrapper path

### 3. Option 2: fully generic public mock module

Redesign `gentest.mock` so the imported public module exports a directly usable,
fully generic `gentest::mock<T>` implementation without later target-local mock
specializations.

```text
clean user source (.cppm)
        |
        v
import gentest.mock
        |
        v
compiler sees complete generic mock<T> implementation immediately
        |
        v
gentest_codegen only discovers tests/fixtures/metadata
        |
        v
final compile links normal generated registration outputs only
```

Pros:

- cleanest user-facing model
- closest to normal “modules remove textual include hacks” expectations
- eliminates the bootstrap mismatch entirely for mocks

Cons:

- much larger redesign
- current generated `MockAccess`/attachment flow would need replacement
- likely changes core mock architecture, behavior surface, and risk profile

## Initial Recommendation

Option 1 is the practical path for this branch series.

Option 2 is architecturally cleaner, but it is large enough to treat as a
separate design effort with its own scope, migration story, and runtime risk
review.

So the proposed sequencing is:

1. remove the user-visible bootstrap block via Option 1
2. keep the current generated mock architecture stable
3. separately evaluate whether a true Option 2 redesign is worth the churn

## Option 1 Breakdown

The stronger review conclusion is that Option 1 is viable only if the
bootstrapped source becomes a shared source-rewrite layer across all module
parsing paths. A ClangTool-only first-parse overlay is not enough.

Ordered subtasks:

1. Extract the module-source rewrite helpers from `tools/src/emit.cpp` into a
   shared utility.
   - Includes:
     - `scan_source_mock_codegen_includes`
     - `find_module_global_fragment_insert_location`
     - mock/bootstrap include block rendering
2. Replace the current parse overlay builder in `tools/src/main.cpp` with a
   true parse-only module bootstrap builder that can inject the required global
   fragment preamble automatically.
3. Make direct-module mock attachment offsets coherent again.
   - Either:
     - preserve a delta between bootstrap source and emitted source, or
     - move emission to the same transformed source basis
4. Apply the same synthesized source model to compiler-driven module work:
   - `clang-scan-deps`
   - manual PCM precompile
   - external/public module resolution
5. Keep the current generated mock architecture intact.
   - The goal is to remove the source-level bootstrap block, not redesign mock
     generation itself.

Option 1 risks:

- direct-module mock insertion offsets can drift if parse-time offsets and
  emit-time source content are no longer aligned
- imported mock-bearing modules may still fail if only the libTooling parse path
  is updated but the compiler precompile path is not
- odd formatting and manual codegen include cases still need to be honored
- same-line directives, multiline directives, header-unit imports, and partition
  shorthand all remain sensitive bootstrap cases

Option 1 test expectations:

- remove the positive `#if defined(GENTEST_CODEGEN)` block from the existing
  module fixtures that currently rely on it
- keep one compatibility regression where the old block is still present and
  verify that auto-bootstrap does not double-inject
- add a new regression for an imported named module that itself uses
  `gentest::mock` or `gentest::asserts` and therefore must survive the compiler
  precompile path with no source-authored bootstrap block

## Option 2 Breakdown

The stronger review conclusion is that Option 2 is not a cleanup. It is a new
mock architecture.

The current implementation depends on generated target-local mock pieces for:

- concrete `mock<T>` specializations
- `MockAccess<mock<T>>` bridges
- ctor/method body generation
- override generation for virtual interfaces
- target-local attachment into named modules

Ordered subtasks:

1. Add a hard feasibility gate.
   - Decide whether the current semantics are required to survive:
     - polymorphic substitution
     - concrete/static/template-member mocking
     - ctor mirroring
     - current diagnostics quality
2. If semantics may change, rewrite `include/gentest/mock.h` so normal builds
   provide a real generic `template<class T> struct mock`.
3. Rewrite `include/gentest/gentest.mock.cppm` to export that real
   implementation instead of mostly re-exporting API names.
4. Replace generated `MockAccess<mock<T>>` with a non-generated mechanism.
5. Rework dispatch and naming so runtime behavior no longer relies on generated
   method bridges.
6. Delete or drastically shrink `tools/src/render_mocks.cpp`.
7. Simplify mock discovery, model metadata, mock domain planning, and wrapper
   injection accordingly.
8. Rebaseline installed package behavior, CMake wiring, docs, and module
   regressions around the new architecture.

Option 2 risks:

- likely feature loss unless codegen or reflection is reintroduced somewhere
- likely diagnostic quality regression
- broad scope across public API, runtime, codegen, installed package behavior,
  and module tests
- much higher migration and breakage risk than Option 1

## TDD / Validation Expectations

Any implementation should keep or extend coverage for:

- module consumers using `import gentest;`
- direct module mocks
- imported sibling module mocks
- module-local mock attachment ordering
- installed package consumers using public modules
- Windows/MSVC and `clang-cl` flavored compile databases
- macOS and Linux module fixtures

Relevant existing regression anchors:

- `tests/consumer/`
- `cmake/CheckCodegenPublicModuleImports.cmake`
- `cmake/CheckModuleMockImportedSibling.cmake`
- `cmake/CheckModuleMockMultiImportedSibling.cmake`
- `cmake/CheckMixedModuleMockRegistry.cmake`

## Open Questions

1. Can Option 1 inject bootstrap support entirely in memory, or does it need a
   temporary on-disk source that behaves like the final wrapper path?
2. Can Option 1 unify its bootstrap include logic with the current wrapper
   injection logic in `tools/src/emit.cpp`, or should the two paths remain
   separate?
3. If Option 2 is ever pursued, can the current generated `MockAccess`
   specialization model be replaced without regressing runtime behavior,
   diagnostics, and matcher ergonomics?
