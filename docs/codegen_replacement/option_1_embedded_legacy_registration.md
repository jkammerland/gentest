# Option 1: embed legacy registration in the authored translation unit

```cpp
#include <gentest/attributes.h>
#include <gentest/runner.h>

namespace calculator_cases {

struct [[using gentest: fixture(suite)]] Database {
    void setUp();
    void tearDown();
};

[[using gentest: test("calculator/add"), fast, req("CALC-1")]]
void addition(Database &database) {
    // Existing Gentest source syntax remains unchanged.
}

[[using gentest: test("calculator/rows"), parameters_pack((lhs, rhs), (1, 10), (2, 20))]]
void rows(Database &database, int lhs, int rhs) {
    // Existing parameter expansion remains Clang-generated.
}

[[using gentest: test("calculator/async")]]
gentest::async_test<void> async_case(Database &database) {
    co_return;
}

[[using gentest: bench("calculator/throughput"), items_per_call(16)]]
void throughput(Database &database) {
    // Fixture setup and teardown remain outside the measured call phase.
}

} // namespace calculator_cases

// The codegen parse defines GENTEST_CODEGEN and deliberately skips this tail.
// A real target compile requires the generated header and fails if it is absent.
#if !defined(GENTEST_CODEGEN)
#include GENTEST_GENERATED_TU_REGISTRATION
#endif
```

```cmake
add_executable(calculator_tests calculator_cases.cpp)
target_link_libraries(calculator_tests PRIVATE gentest::gentest_main)

# Proposed API.
gentest_attach_codegen(calculator_tests
    EMBEDDED_REGISTRATION
    SOURCES calculator_cases.cpp)

gentest_discover_tests(calculator_tests)
```

## Status

Proposed. This is the preferred immediate branch when preserving the current
`[[using gentest: ...]]` API is more important than removing Clang tooling in
one step.

## User-facing contract

Users keep the current attributes, function bodies, fixtures, parameters,
templates, async cases, benches, jitters, death cases, and explicit mock
targets. Each textual test translation unit adds one terminal include anchor.

The authored `.cpp` remains the source compiled by CMake. Gentest no longer
generates a wrapper `.cpp` that includes and replaces the authored source.

The footer is fail-closed during the real compile:

- codegen parses the source with `GENTEST_CODEGEN=1`, so it does not require its
  own output before producing that output;
- the real compiler does not define `GENTEST_CODEGEN`, so a missing generated
  registration header is a hard compilation failure;
- CMake adds a dependency from the authored source's object build to its
  generated header.

Do not use `__has_include` in the real compilation path. It would allow an
incorrectly ordered build to silently produce a binary with no registered
tests.

## What the generated header contains

The generated header remains tail code. Conceptually it contains:

```cpp
// calculator_cases.gentest.h -- generated, illustrative only
#pragma once

#include <gentest/detail/generated_runtime.h>

namespace {

void gentest_invoke_addition(void *context) {
    auto &database = gentest::detail::fixture_from_context<calculator_cases::Database>(context);
    calculator_cases::addition(database);
}

inline constexpr gentest::Case cases[] = {
    {
        .name = "calculator/add",
        .fn = &gentest_invoke_addition,
        .file = "calculator_cases.cpp",
        .line = 13,
        .tags = gentest_generated_tags_0,
        .requirements = gentest_generated_requirements_0,
        .fixture_lifetime = gentest::FixtureLifetime::None,
        .suite = "calculator",
    },
    // Parameter, async, benchmark, and jitter expansions follow.
};

struct GentestTranslationUnitRegistrar {
    GentestTranslationUnitRegistrar() {
        gentest::detail::register_cases(cases);
    }
};

[[maybe_unused]] const GentestTranslationUnitRegistrar registrar{};

} // namespace
```

The exact generated implementation should reuse the current rendering and
runtime contracts initially. This option changes output placement and CMake
attachment, not test semantics.

## Build graph

```text
calculator_cases.cpp
        |
        | scanned with its real target compile context and GENTEST_CODEGEN=1
        v
gentest_codegen
        |
        v
<target-specific-root>/calculator_cases.gentest.h
        |
        | hard dependency
        v
compile calculator_cases.cpp as the target's real object
```

The root `compile_commands.json` should consequently contain an entry whose
`file` is the authored `calculator_cases.cpp`. It should not contain a
`tu_*.gentest.cpp` substitute for that source.

This immediately removes the need for the current clang-tidy logic that opens
a wrapper, finds its first quoted include, and synthesizes a command for the
authored source. clangd sees the actual file, compiler flags, target defines,
include paths, and per-source options.

## Target-specific include resolution

The source must not bake a target name or absolute build path into the include:

```cpp
#if !defined(GENTEST_CODEGEN)
#include GENTEST_GENERATED_TU_REGISTRATION
#endif
```

CMake supplies a quoted header token for each compilation:

```text
-DGENTEST_GENERATED_TU_REGISTRATION="gentest/calculator_tests/9f21c4a7.gentest.h"
```

The output key should be derived from a normalized project-relative source key,
not only from the basename. This prevents collisions between:

```text
tests/unit/cases.cpp
tests/integration/cases.cpp
```

It also allows one authored source to be compiled in multiple targets with
different flags and therefore different generated registrations.

## Capabilities retained

Because the generated header is included after the owning source, it can still
name entities that would not be visible from a standalone generated
translation unit:

```cpp
namespace {

struct LocalFixture {
    int value = 42;
};

[[using gentest: test("private/static")]]
static void source_local(LocalFixture &fixture) {
    gentest::expect_eq(fixture.value, 42);
}

} // namespace

#if !defined(GENTEST_CODEGEN)
#include GENTEST_GENERATED_TU_REGISTRATION
#endif
```

This retains:

- anonymous-namespace and `static` cases;
- source-local fixture and helper types;
- local, suite, and global fixture inference;
- allocation hooks and sync/async setup and teardown;
- parameter axes, row packs, numeric ranges, and template matrices;
- test, benchmark, jitter, death, skip, owner, requirement, and tag metadata;
- current source-name inference and source locations;
- generated mocks and mock attachment through explicit mock targets;
- build-time target-wide duplicate-name diagnostics;
- current depfile, artifact-manifest, and inventory generation.

## What this option does not remove

The legacy syntax still requires semantic C++ inspection. Gentest must retain:

- a host Clang/LLVM dependency;
- target compile flags, defines, include paths, resource directories, and
  module mappings;
- compilation-database staging and active-config filtering where currently
  required;
- semantic discovery for function signatures, fixtures, templates, parameter
  names, and mocks;
- codegen invalidation when compile context or reachable headers change.

This option fixes the authored-source compilation database. It does not make
the legacy parser independent of a compilation database.

## Why a lexical scanner is not substituted here

The current surface permits real C++ around and inside metadata:

```cpp
#if FEATURE_ENABLED
[[using gentest: test("feature/enabled")]]
void enabled();
#endif

template<class T>
requires Enabled<T>
[[using gentest: test("feature/template"), template(T, int, long)]]
void constrained();
```

A lexical scanner cannot reliably know active preprocessor branches, expanded
macros, aliases, overload resolution, constraints, source-local types, or the
target's module state without recreating compiler semantics. Option 1 keeps the
semantic parser deliberately and removes only the wrapper ownership trick.

## Modules

Do not append the textual footer protocol blindly to named module interfaces.
The initial implementation should keep the existing additive same-module
registration units for `MODULE_REGISTRATION` sources.

For a future descriptor/reflection module backend, registration can live in an
owned implementation unit:

```cpp
module calculator.tests;
import gentest;

namespace calculator_module_cases {
// Authored cases and an explicit descriptor/reflection registration point.
}
```

Textual headers that must be attached to the global module belong before the
module declaration. Generated registration that names non-exported entities
must be attached to the same module and cannot be modeled as an unrelated
standalone `.cpp`.

## CMake implementation boundary

`EMBEDDED_REGISTRATION` should be an explicit attachment strategy, not a flag
sprinkled through the existing wrapper path. Suggested internal phases:

1. Collect and normalize owner sources.
2. Resolve the real compile context and stage it content-stably.
3. Plan target-specific generated header, depfile, manifest, inventory, and
   mock outputs.
4. Invoke codegen on the authored source with `GENTEST_CODEGEN=1`.
5. Add the generated root to the target include path.
6. Add the per-source include macro and object dependency.
7. Leave target `SOURCES` and module file sets unchanged.

The artifact manifest must describe the new contract honestly, for example:

```json
{
  "kind": "cxx-owner-tail-registration",
  "owner_source": "/src/tests/calculator_cases.cpp",
  "generated_headers": [
    "/build/gentest/calculator_tests/9f21c4a7.gentest.h"
  ],
  "includes_owner_source": false,
  "replaces_owner_source": false
}
```

It must not reuse wrapper manifest values such as
`includes_owner_source: true` or `replaces_owner_source: true`.

## Failure behavior

The implementation should diagnose these cases directly:

```text
calculator_cases.cpp: missing terminal Gentest registration anchor
calculator_cases.cpp: registration anchor appears before the last discovered case
calculator_cases.cpp: embedded registration was requested by two incompatible target contexts
calculator_cases.cpp: generated include macro is user-defined
```

A scan may validate the anchor with preprocessor callbacks. CMake should not
grow a second C++ text parser for this purpose.

## Validation requirements

At minimum, the branch implementing this option should prove:

- a clean configure/build/test of ordinary cases;
- authored sources, not wrappers, appear in `compile_commands.json`;
- clang-tidy runs without owner-source remapping;
- anonymous namespaces, `static` cases, and source-local fixture types work;
- local/suite/global fixtures retain setup, call, teardown, repeat, filter, and
  shuffle behavior;
- parameter axes, row packs, ranges, and template matrices retain names/counts;
- async setup/test/teardown and blocked/failed paths retain behavior;
- benchmark/jitter setup and teardown remain outside measured loops;
- generated textual mocks retain overload, const/ref, and `noexcept` behavior;
- two same-basename sources do not collide;
- one source compiled by two targets receives distinct generated headers;
- no-op reconfigure does not rerun codegen;
- compiler define/include changes do rerun codegen;
- deleting a generated header recovers correctly;
- the existing module-registration path remains green;
- CTest `--list` discovery produces exactly the prior inventory.

## Migration

This option intentionally asks every textual test source to change once. A
codemod can append the guarded include anchor, after which targets opt into
`EMBEDDED_REGISTRATION` incrementally.

During migration, wrapper and embedded strategies may coexist across targets,
but one owner source must use exactly one attachment strategy in a given
target. After the repository and downstream examples migrate, embedded mode
can become the default in a major release and wrapper mode can enter a defined
compatibility window.
