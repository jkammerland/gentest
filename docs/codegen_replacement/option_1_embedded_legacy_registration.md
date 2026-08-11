# Option 1: embed legacy registration in the authored translation unit

```cpp
#include <gentest/attributes.h>
#include <gentest/runner.h>

namespace calculator {

struct [[using gentest: fixture(suite)]] Database
    : gentest::FixtureSetup,
      gentest::FixtureTearDown {
    void setUp() override;
    void tearDown() override;
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

} // namespace calculator

// The codegen parse defines GENTEST_CODEGEN and deliberately skips this tail.
// A real target compile requires the generated header and fails if it is absent.
#define GENTEST_TU_ID calculator_cases
#if !defined(GENTEST_CODEGEN)
#include <gentest/registration_tail.h>
#endif
#undef GENTEST_TU_ID
```

```cmake
add_executable(calculator_tests calculator_cases.cpp)
target_link_libraries(calculator_tests PRIVATE gentest::gentest_main)

# Proposed API.
gentest_attach_codegen(calculator_tests
    EMBEDDED_REGISTRATION
    SOURCES calculator_cases.cpp
    SOURCE_IDS "calculator_cases.cpp=calculator_cases")

gentest_discover_tests(calculator_tests)
```

## Status

Proposed. This is the preferred immediate branch when preserving the current
`[[using gentest: ...]]` API is more important than removing Clang tooling in
one step.

## User-facing contract

Users keep the current attributes, function bodies, fixtures, parameters,
templates, async cases, benches, jitters, death cases, and explicit mock
targets. Each ordinary textual test translation unit without module imports
adds one terminal include anchor. Importing textual sources retain wrapper
compatibility initially, as described below.

The authored `.cpp` remains the source compiled by CMake. Gentest no longer
generates a wrapper `.cpp` that includes and replaces the authored source.

The footer is fail-closed during the real compile:

- codegen parses the source with `GENTEST_CODEGEN=1`, so it does not require its
  own output before producing that output;
- the real compiler does not define `GENTEST_CODEGEN`, so a missing generated
  registration header is a hard compilation failure;
- CMake adds a target-level dependency that generates every embedded header
  before compiling any authored owner object.

Do not use `__has_include` in the real compilation path. It would allow an
incorrectly ordered build to silently produce a binary with no registered
tests.

## What the generated header contains

The generated header remains tail code. Conceptually it contains:

```cpp
// calculator_cases.gentest.h -- generated, illustrative only
#pragma once

// registration_tail.h or an earlier preamble supplies generated-runtime
// support before this target-private file is included.
namespace gentest::generated::calculator_cases {

static void gentest_invoke_addition(void *context) {
    auto *database = static_cast<calculator::Database *>(context);
    if (database == nullptr) {
        gentest_record_fixture_failure("calculator::Database", "instance missing");
        return;
    }
    calculator::addition(*database);
}

inline constexpr gentest::Case cases[] = {
    {
        .name = "calculator/add",
        .fn = &gentest_invoke_addition,
        .file = "calculator_cases.cpp",
        .line = 13,
        .tags = gentest_generated_tags_0,
        .requirements = gentest_generated_requirements_0,
        .fixture = "calculator::Database",
        .fixture_lifetime = gentest::FixtureLifetime::MemberSuite,
        .suite = "calculator",
    },
    // Parameter, async, benchmark, and jitter expansions follow.
};

struct GentestTranslationUnitRegistrar {
    GentestTranslationUnitRegistrar() {
        gentest::detail::register_shared_fixture<calculator::Database>(
            gentest::detail::SharedFixtureScope::Suite,
            "calculator",
            "calculator::Database");
        gentest::detail::register_cases(cases);
    }
};

[[maybe_unused]] const GentestTranslationUnitRegistrar registrar{};

} // namespace gentest::generated::calculator_cases
```

This is a reduced illustration of the current contract, not a new public API.
In particular, shared fixtures require both a non-`None` `Case` lifetime and a
matching `register_shared_fixture<T>(scope, owner_suite, fixture_name)` call.
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

The source must not bake a target name or absolute build path into the include.
It supplies a stable TU identifier instead:

```cpp
#define GENTEST_TU_ID calculator_cases
#if !defined(GENTEST_CODEGEN)
#include <gentest/registration_tail.h>
#endif
#undef GENTEST_TU_ID
```

The same identifier is declared explicitly to CMake so output paths are known
at configure time:

```cmake
gentest_attach_codegen(calculator_tests
    EMBEDDED_REGISTRATION
    SOURCES calculator_cases.cpp parser_cases.cpp
    SOURCE_IDS
        "calculator_cases.cpp=calculator_cases"
        "parser_cases.cpp=parser_cases")
```

CMake validates that every embedded source has exactly one unique ID and can
then declare all dispatcher, leaf-header, depfile, and manifest outputs without
parsing C++ or waiting for codegen.

The source defines `GENTEST_TU_ID` outside the `GENTEST_CODEGEN` include guard.
The Clang preprocessor callback therefore observes and validates it against
`SOURCE_IDS` during the scan, before emitting outputs; CMake itself does not
parse C++.

Each target generates its own private `gentest/registration_tail.h` dispatcher.
Conceptually it selects the leaf header from the source-owned token:

```cpp
// Target-private generated registration_tail.h, simplified.
#define GENTEST_DETAIL_ID_calculator_cases 1
#define GENTEST_DETAIL_ID_parser_cases     2
#define GENTEST_DETAIL_JOIN_IMPL(lhs, rhs) lhs##rhs
#define GENTEST_DETAIL_JOIN(lhs, rhs) GENTEST_DETAIL_JOIN_IMPL(lhs, rhs)
#define GENTEST_DETAIL_SELECTED_ID(id) GENTEST_DETAIL_JOIN(GENTEST_DETAIL_ID_, id)

#if GENTEST_DETAIL_SELECTED_ID(GENTEST_TU_ID) == 1
#include <gentest/generated/calculator_cases.gentest.h>
#elif GENTEST_DETAIL_SELECTED_ID(GENTEST_TU_ID) == 2
#include <gentest/generated/parser_cases.gentest.h>
#else
#error "GENTEST_TU_ID is not declared in gentest_attach_codegen(SOURCE_IDS)"
#endif
```

Every target receives its own private generated include root, containing for
example `gentest/generated/calculator_cases.gentest.h`. The same source and TU
identifier can therefore be compiled by two targets while resolving to two
different target-owned headers. CMake uses a target-level dependency so all of
that target's embedded headers exist before any owner compilation; it does not
depend on directory-scoped source `COMPILE_DEFINITIONS` or `OBJECT_DEPENDS`.

TU identifiers must be unique within one target and should be stable under
build-directory moves. CMake catches duplicate mappings between:

```text
tests/unit/cases.cpp
tests/integration/cases.cpp
```

Projects may choose readable explicit IDs as above or let a codemod derive an
ID from the normalized project-relative source path. The codemod updates both
the footer and `SOURCE_IDS`. The codegen preprocessor callback diagnoses a
source/CMake mismatch, while the generated dispatcher rejects any undeclared
ID that reaches a real compile.
The identifier, source key, and target-private root together define the output
identity.

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

#define GENTEST_TU_ID private_static_cases
#if !defined(GENTEST_CODEGEN)
#include <gentest/registration_tail.h>
#endif
#undef GENTEST_TU_ID
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

## Ordinary `.cpp` sources that import modules

An ordinary textual `.cpp` containing `import` is a separate compatibility
case. The current wrapper injects Gentest/standard support before the first
import and suppresses the generated tail header's preamble; GCC can reject
repeated textual declarations introduced after importing the same facilities.

The first embedded implementation should detect this case and keep it on
wrapper mode with a focused message instead of emitting an invalid tail:

```text
import_only.cpp: embedded registration does not yet support textual sources
that import modules; use wrapper compatibility mode
```

A follow-up may add an explicit two-anchor protocol:

```cpp
#include <gentest/registration_preamble.h> // before the first import
import calculator;

[[using gentest: test("calculator/imported")]]
void imported_case();

#define GENTEST_TU_ID calculator_import_cases
#define GENTEST_TU_REGISTRATION_NO_PREAMBLE 1
#if !defined(GENTEST_CODEGEN)
#include <gentest/registration_tail.h>
#endif
#undef GENTEST_TU_REGISTRATION_NO_PREAMBLE
#undef GENTEST_TU_ID
```

That protocol needs its own GCC/Clang/module dependency tests. It is not implied
by the single-footer ordinary textual design.

## Named module units

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
5. Add the target-private generated root to the target include path.
6. Add a target-level dependency that materializes all embedded headers before
   compiling any owner source.
7. Mark every embedded owner source `SKIP_UNITY_BUILD_INCLUSION ON`. Combining
   multiple owners into one unity TU can collide internal names and can make one
   dispatcher include suppress another.
8. Leave target `SOURCES` and module file sets unchanged.

The artifact manifest must describe the new contract honestly, for example:

```json
{
  "kind": "cxx-owner-tail-registration",
  "owner_source": "/src/tests/calculator_cases.cpp",
  "generated_headers": [
    "/build/gentest/calculator_tests/include/gentest/generated/calculator_cases.gentest.h"
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
calculator_cases.cpp: duplicate GENTEST_TU_ID 'calculator_cases' in this target
calculator_cases.cpp: source GENTEST_TU_ID does not match SOURCE_IDS mapping
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
- configure rejects missing/duplicate `SOURCE_IDS`, codegen diagnoses a
  source/footer mismatch, and the dispatcher rejects undeclared IDs;
- a target with `UNITY_BUILD ON` still compiles each embedded owner separately;
- ordinary `.cpp` sources that import modules remain on wrapper mode with a
  focused diagnostic, and the existing import-only regression remains green;
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

## Repository anchors

- [current wrapper construction and source replacement](../../cmake/gentest/TuMode.cmake)
- [current generated registration and wrapper templates](../../tools/src/templates.hpp)
- [clang-tidy owner-source remapping](../../scripts/check_clang_tidy.sh)
- [why a standalone declaration-only registration TU was rejected](../stories/036_textual_declaration_only_registration.md)
