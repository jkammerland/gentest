# Recommendation: remove wrappers first, then converge on typed adapters

```cpp
// Immediate compatibility path: option 1.
[[using gentest: test("calculator/addition")]]
void addition(Database &database) {
    // Existing body and semantics.
}

#if !defined(GENTEST_CODEGEN)
#include GENTEST_GENERATED_TU_REGISTRATION
#endif
```

```cpp
// Portable long-term path: option 2.
void addition(Database &database);

const auto registration =
    gentest::test("calculator/addition")
        .run<&addition>();
```

```cpp
// Optional C++26 frontend over the same adapters: option 3.
namespace calculator_case_tu {

[[= gentest::reflection::test("calculator/addition")]]
void addition(Database &database);

} // namespace calculator_case_tu

GENTEST_REFLECT_REGISTER(calculator_case_tu)
```

## Decision

Implement option 1 first: compile authored `.cpp` files directly and require a
guarded, fail-closed generated registration include at the end of each legacy
textual test source.

While doing that, begin extracting the generated wrapper algorithms into a
backend-neutral C++20 typed adapter core. That core becomes option 2's public
header-only API and option 3's reflection implementation layer.

C++26 reflection should remain an optional frontend until the supported
compiler and language-server set can parse the final syntax. A simple lexical
scanner should be a codemod or deliberately restricted compatibility tool, not
the permanent semantic backend. Automatic mock generation remains a separate
decision.

## The three branch points

| Option | Source compatibility | Removes wrapper/clangd hacks | Removes Clang tooling | Long-term role |
| --- | --- | --- | --- | --- |
| [1. Embedded legacy registration](option_1_embedded_legacy_registration.md) | Yes, plus one footer | Yes | No | Immediate migration and compatibility backend |
| [2. Header-only typed descriptors](option_2_header_only_descriptors.md) | No; new API | Yes | Yes for registration | Recommended portable core and primary future API |
| [3. C++26 typed reflection](option_3_cpp26_reflection.md) | No; new annotations | Yes | Yes for supported cases | Optional ergonomic frontend over option 2 |

## Why option 1 comes first

The wrapper source replacement is the direct cause of the current development
tooling problems. CMake compiles a generated `.gentest.cpp`, so the authored
`.cpp` lacks its own compilation-database entry. clang-tidy then has to recover
the owner from an include inside the wrapper, and clangd must borrow or infer a
command.

Option 1 removes that structural problem without simultaneously rewriting:

- fixture ownership and allocation;
- parameter and template expansion;
- async adapters;
- benchmark/jitter phase handling;
- death-test metadata;
- mock generation;
- source compatibility for all existing test suites.

The source change is mechanical and visible:

```cpp
#if !defined(GENTEST_CODEGEN)
#include GENTEST_GENERATED_TU_REGISTRATION
#endif
```

The build result is materially simpler:

```text
before:
  authored.cpp -> generated wrapper.cpp includes authored.cpp -> compile wrapper.cpp

after:
  authored.cpp -> generated tail header -> compile authored.cpp
```

The generator still receives the exact target compilation context, but the
root compilation database describes the real source again.

## Follow option 1 with adapter extraction

The current generated header contains large source-rendered wrappers for
fixtures, parameters, async cases, and measured phases. Option 1 should avoid
redesigning them in its first patch, but its output should move progressively
toward small typed calls:

```cpp
// Earlier generated shape: substantial source-rendered wrapper logic.
static void generated_wrapper(void *context) {
    // allocation, setup, conversion, invocation, teardown...
}

// Desired generated shape: data and template instantiation.
using GeneratedCase = gentest::detail::BoundCase<
    &calculator_cases::addition,
    gentest::detail::case_spec<"calculator/addition">,
    gentest::detail::fixture_binding<0, Database, gentest::suite_fixture>,
    gentest::detail::value_axis<1, 1, 2>>;
```

This yields one implementation of the difficult runtime semantics:

```text
legacy Clang attributes ----+
                            |
C++20 descriptor API -------+--> typed adapters --> gentest::Case registry
                            |
C++26 reflection -----------+
```

Benefits:

- legacy generation becomes smaller and less string-oriented;
- option 2 can expose the same adapters directly;
- option 3 only performs discovery/metadata reflection;
- fixture, async, and measured edge-case fixes apply to every frontend;
- generated and non-generated cases can coexist during migration.

## Recommended user-facing destination

The ordinary-function descriptor form should be the primary portable API:

```cpp
namespace service_cases {

gentest::async_test<void> reconnect(
    ServiceFixture &service,
    std::chrono::milliseconds timeout) {
    co_await service.reconnect(timeout);
    gentest::expect_true(service.connected());
}

const auto reconnect_cases =
    gentest::test("service/reconnect")
        .tag("network")
        .requirement("NET-42")
        .values<1>(100ms, 500ms)
        .run<&reconnect>();

} // namespace service_cases
```

It is standard C++ that GCC, Clang, MSVC, clangd, clang-tidy, and ordinary
refactoring tools can understand without Gentest preprocessing.

Reflection should make that API more concise where supported:

```cpp
namespace service_case_tu {

[[= gentest::reflection::test("service/reconnect")
        .tag("network")
        .requirement("NET-42")]]
gentest::async_test<void> reconnect(
    ServiceFixture &service,
    [[= gentest::reflection::values(100ms, 500ms)]]
    std::chrono::milliseconds timeout);

} // namespace service_case_tu

GENTEST_REFLECT_REGISTER(service_case_tu)
```

The reflection frontend must not have a separate execution model.

## Registration transport

Use ordinary static registration for the mainstream API because the existing
runtime already follows that model. Add explicit bundles for environments that
avoid dynamic initialization or static-archive retention surprises:

```cpp
int main(int argc, char **argv) {
    return gentest::run_cases(
        gentest::combine(serviceCases(), databaseCases()),
        argc,
        argv);
}
```

Do not choose linker sections by default. They add platform-specific retention,
archive, LTO, shared-library, and sanitizer behavior without solving callable
adaptation or metadata.

## Duplicate cases

Retain build-time cross-TU duplicate diagnostics while the legacy generator
merges a target-wide inventory. For header-only and reflection registrars, add
deterministic runtime validation before both `--list` and execution:

```text
duplicate Gentest case name: service/reconnect[500ms]
  service_cases.cpp:31
  fallback_service_cases.cpp:18
```

Compile-time checking remains possible within a single explicit bundle or one
reflection footer.

## Mocks

Do not hold registration cleanup hostage to mock replacement. Option 1 can keep
the existing explicit mock targets and Clang generator.

Track the long-term mock decision independently:

```cpp
struct ServiceMock final : Service {
    GENTEST_MOCK_METHOD(int, compute, (int value), (const, override));
};
```

Possible destinations are explicit Gentest method macros, direct
gMock/Trompeloeil use, handwritten fakes, or a shared interface schema. Final
C++26 reflection cannot synthesize arbitrary virtual overrides and bodies.

## Recommended implementation sequence

1. Add option 1 as an explicit `EMBEDDED_REGISTRATION` strategy for textual
   sources.
2. Keep target `SOURCES` unchanged and scan the authored `.cpp` directly.
3. Generate one target-specific tail header per source with a hard object
   dependency.
4. Add focused anchor validation and truthful artifact-manifest semantics.
5. Prove exact test inventory and behavior parity against wrapper mode.
6. Remove clang-tidy owner remapping for embedded targets and verify authored
   compilation-database entries.
7. Migrate repository tests and downstream examples mechanically.
8. Make embedded mode the next major version's default; retain wrapper mode for
   one explicit compatibility window.
9. Extract wrapper behavior into the C++20 typed adapter core.
10. Add option 2 descriptors and mixed-backend tests.
11. Extend option 3 to fixtures, parameters, async, and measured cases by using
    the same adapters.
12. Decide and migrate automatic mocks separately.
13. Remove the legacy Clang package only when old attributes and generated mocks
    no longer require it.

## First implementation issue scope

The first issue should remain narrowly deliverable:

```text
Implement embedded registration for ordinary textual .cpp sources.
Do not remove legacy Clang parsing.
Do not redesign mocks.
Do not replace the module-registration backend.
Do not make descriptor/reflection parity a blocker.
```

It should still lay compatible foundations:

- an explicit attachment-strategy boundary in CMake;
- output manifests that do not claim the owner is replaced;
- generated headers structured to permit later typed-adapter extraction;
- mixed wrapper/embedded targets during migration;
- tests that prove compile-database and clang-tidy simplification.

## Exit criteria for the recommendation

The recommended direction is successful when:

- textual targets compile authored `.cpp` files directly;
- clangd and clang-tidy receive exact authored-source commands;
- no Gentest wrapper source is required for textual registration;
- legacy behavior remains equivalent during migration;
- new descriptor targets install and build without Clang tooling;
- reflection is an interchangeable frontend rather than a separate runtime;
- only legacy syntax and unresolved automatic mock generation keep Clang alive.
