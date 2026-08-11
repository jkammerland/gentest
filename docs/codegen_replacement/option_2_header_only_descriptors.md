# Option 2: header-only typed descriptors

```cpp
#include <gentest/register.h>
#include <gentest/test.h>

namespace calculator_cases {

void addition(Database &database, int value) {
    gentest::expect_eq(database.add(value), value + 1);
}

[[maybe_unused]] const auto registration =
    gentest::test("calculator/addition")
        .tag("fast")
        .requirement("CALC-1")
        .values<1>(1, 2)
        .run<&addition>();

} // namespace calculator_cases
```

```cmake
add_executable(calculator_tests calculator_cases.cpp)
target_link_libraries(calculator_tests PRIVATE gentest::gentest_main)
gentest_discover_tests(calculator_tests)

# No gentest_attach_codegen(), host Clang, generated source, or Gentest use of
# compile_commands.json.
```

## Status

Proposed. This is the recommended portable long-term registration core, but it
is a source redesign rather than a compatibility mode.

All APIs in this document are illustrative and should be validated with focused
prototypes before their names become public contracts.

## User-facing cases

Ordinary functions retain normal C++ navigation and refactoring behavior:

```cpp
void parse(ParserFixture &parser, std::string_view input) {
    gentest::expect_true(parser.accepts(input));
}

const auto parse_cases =
    gentest::test("parser/accepts")
        .owner("parser-team")
        .values<1>("number", "identifier")
        .run<&parse>();
```

Row-wise parameters are explicit and typed:

```cpp
void rows(Database &database, int key, std::string_view expected);

const auto row_cases =
    gentest::test("database/rows")
        .rows<1, 2>(
            gentest::row(1, "one"),
            gentest::row(2, "two"))
        .run<&rows>();
```

Async classification follows the return type:

```cpp
gentest::async_test<void> fetch(NetworkFixture &network) {
    co_await network.ready();
    gentest::expect_true(network.connected());
}

const auto fetch_case =
    gentest::test("network/fetch")
        .run<&fetch>();
```

Measured cases select the current runtime phase protocol:

```cpp
void parse_batch(ParserFixture &parser) {
    auto result = parser.parseBatch();
    gentest::doNotOptimizeAway(result);
}

const auto parse_benchmark =
    gentest::bench("parser/throughput")
        .itemsPerCall(16)
        .baseline()
        .run<&parse_batch>();
```

Fixture lifetime and declaring-suite identity move from parsed source structure
to a typed binding. Both are required: current suite fixtures belong to their
declaring namespace and are shared with that namespace's descendant suites.

```cpp
namespace calculator {

struct Database : gentest::FixtureSetup, gentest::FixtureTearDown {
    void setUp() override;
    void tearDown() override;
};

} // namespace calculator

template<>
inline constexpr auto gentest::fixture_policy_v<calculator::Database> =
    gentest::fixture_policy{
        .lifetime = gentest::fixture_scope::suite,
        .owner_suite = "calculator",
        .identity = "calculator::Database",
    };
```

An optional per-case override avoids global specialization when the same type
needs different ownership policies. It must provide a complete binding key,
not only a lifetime:

```cpp
const auto case_with_override =
    gentest::test("database/local")
        .fixture<0>(gentest::fixture_binding{
            .lifetime = gentest::fixture_scope::local,
            .owner_suite = "database",
            .identity = "database/local/Database",
        })
        .run<&uses_database>();
```

For suite fixtures, resolution matches the owner suite itself and descendants
at a namespace separator boundary. `calculator` may serve `calculator/parser`
but must not serve `calculator_extra`. Registry identity includes lifetime,
owner suite, fixture identity, and callback/type contract. Conflicting bindings
for one identity must fail deterministically before the run.

Reusing one C++ type in multiple owner scopes is allowed only with explicit,
distinct binding identities. This avoids silently merging state that the
current namespace-scoped model keeps separate.

## Template matrices

C++20 cannot pass an uninstantiated function template as a function-pointer
non-type template argument. Use a callable class or explicit specializations:

```cpp
struct MatrixCase {
    template<class T, int N>
    static void run() {
        static_assert(sizeof(T) >= 1);
        gentest::expect_true(N > 0);
    }
};

const auto matrix_cases =
    gentest::template_test<MatrixCase>("matrix/basic")
        .types<int, long>()
        .values<1, 2>();
```

This changes the declaration style but makes the matrix type-safe without a
compiler frontend.

## Optional concise macro frontend

A macro can retain the “metadata and body in one block” feel:

```cpp
GENTEST_TEST(
    addition,
    (Database &database, int value),
    gentest::test("calculator/addition").values<1>(1, 2)) {
    gentest::expect_eq(database.add(value), value + 1);
}
```

Conceptually it expands to a forward declaration, a typed static registrar, and
the function definition. It must remain optional because:

- parameter lists containing commas require parenthesized macro arguments;
- diagnostics may expose macro expansions;
- rename/refactoring is weaker than the ordinary-function form;
- macros are not exported by named modules.

## Backend-neutral adapter core

The core abstraction should bind one callable plus one structural case spec:

```cpp
template<auto Callable, class Spec>
class StaticRegistration;

template<auto Callable>
auto bind(
    CaseSpec spec,
    std::source_location location = std::source_location::current());
```

The adapter can use C++20 function traits and concepts to:

1. Decompose the function pointer's argument and return types.
2. Match explicitly bound parameter indices.
3. Treat remaining supported argument forms as fixtures.
4. Resolve the binding's lifetime, declaring/owner suite, fixture identity, and
   descendant-matching policy.
5. Instantiate local handles or register/look up shared handles under that full
   identity.
6. Select synchronous, asynchronous, benchmark, or jitter invocation.
7. Produce stable descriptor storage and one or more runtime `Case` objects.
8. Register those cases through the existing runtime registry.

The generated backend and the C++26 reflection backend should eventually call
the same adapters instead of maintaining separate wrapper implementations.

## Diagnostics

Concept-constrained builders should provide focused source errors:

```text
gentest::test("calculator/addition"): parameter 1 has neither a value binding nor a supported fixture policy
gentest::bench("parser/throughput"): async_test return types are not supported for measured cases
gentest::test("overloaded"): callable is overloaded; use gentest::overload<Signature>(&function)
gentest::test("defaults"): default arguments are not visible through the callable type; bind parameter 2 explicitly
```

Example overload selection:

```cpp
void parse(Database &, int);
void parse(Database &, std::string_view);

const auto integer_parse =
    gentest::test("parse/integer")
        .run<gentest::overload<void(Database &, int)>(&parse)>();
```

## Explicit bundles

Static registration is convenient, but an explicit bundle should exist for
embedded systems, plugins, and static libraries where constructors or archive
retention are undesirable:

```cpp
// calculator_case_bundle.cpp
gentest::CaseBundle calculatorCases() {
    return gentest::cases(
        gentest::test("calculator/addition").bind<&addition>(),
        gentest::bench("calculator/throughput").bind<&throughput>());
}
```

```cpp
// main.cpp
int main(int argc, char **argv) {
    return gentest::run_cases(
        gentest::combine(calculatorCases(), parserCases()),
        argc,
        argv);
}
```

Bundles provide deterministic ownership and compile-time duplicate checking
inside a bundle. Aggregation across translation units remains explicit.

## Duplicate-name policy

Standard C++ cannot portably issue a compile-time diagnostic for equal string
names registered by separately compiled translation units.

The runner should therefore validate before both listing and execution:

```text
duplicate Gentest case name: calculator/addition
  first registered at calculator_a.cpp:17
  also registered at calculator_b.cpp:42
```

Do not throw from static initialization. Registrars should collect cases; the
checked registry should sort and validate deterministically before `--list` or
the first run.

## Static registration caveats

An object file containing only a static registrar may be omitted when it lives
inside a static archive and no symbol pulls that object into the final link.
Supported responses are:

- document OBJECT-library or whole-archive linkage for automatic registration;
- provide an explicit force-link symbol per case bundle;
- use explicit bundles instead of static registration.

Linker sections are not the default answer. They require separate ELF, Mach-O,
and COFF implementations plus retention handling for LTO, dead stripping,
archives, and shared libraries.

## Named modules

Descriptors are ordinary C++ and are the cleanest module-facing option:

```cpp
module;
#include <gentest/register.h>

export module calculator.tests;
import gentest;

namespace calculator_tests {

void addition() {
    gentest::expect_true(2 + 2 == 4);
}

const auto registration =
    gentest::test("calculator/addition").run<&addition>();

} // namespace calculator_tests
```

A future `gentest.registration` named module can remove the global-module
fragment. No generated same-module implementation unit is required for the
descriptor path.

## What works

- ordinary tests, benches, and jitters;
- metadata, source location, selection, reporting, and CTest discovery;
- local/suite/global fixtures and allocation hooks;
- declaring-suite ownership, descendant matching, and conflict detection for
  shared fixtures;
- sync and async fixture/test adapters;
- Cartesian parameters, row packs, and typed numeric generators;
- function-template matrices through callable adapters;
- mixed descriptor and generated registrations during migration;
- C++20 GCC, Clang, and MSVC source parsing;
- named modules without generated registration units;
- no Gentest codegen dependency on `compile_commands.json`.

## What does not work automatically

- preserving `[[using gentest: ...]]` without a migration;
- inferring default arguments from a function pointer;
- selecting an overload without a signature;
- compile-time duplicate detection across translation units;
- retaining registrar-only object files discarded from static archives;
- synthesizing arbitrary mock override declarations from an existing class.

## Mock boundary

Without a semantic generator, users must write or macro-declare the mock
surface:

```cpp
struct ServiceMock final : Service {
    GENTEST_MOCK_METHOD(int, compute, (int value), (const, override));
    GENTEST_MOCK_METHOD(void, reset, (), (noexcept, override));
};
```

Direct gMock/Trompeloeil use, handwritten fakes, or a shared X-macro method
schema are also viable. This is a separate product decision from registration.

## Validation requirements

The descriptor core needs parity tests for:

- metadata and exact `--list` inventory;
- empty and invalid value axes;
- Cartesian and row-wise naming/order;
- mixed fixture/value argument positions;
- allocation, setup, call, teardown, and failure precedence;
- suite/global scoping, repeat, filter, shuffle, and parallel execution;
- exact-owner and descendant shared-fixture matches, prefix non-matches, and
  one C++ fixture type bound explicitly in multiple owner scopes;
- async completion, timeout, cancellation, blocked dependencies, and teardown;
- benchmark/jitter phase separation and items-per-call normalization;
- overload/default-argument diagnostics;
- duplicate names within and across translation units;
- static archive retention and explicit bundles;
- mixed legacy-generated and descriptor cases in one executable;
- include and named-module consumers.

## Repository anchors

- [runtime `Case` descriptor](../../include/gentest/detail/case_api.h)
- [current fixture and generated-runtime helpers](../../include/gentest/detail/generated_runtime.h)
- [runtime case registry](../../src/runner_impl.cpp)
- [shared-fixture owner/descendant matching](../../src/runner_fixture_runtime.cpp)
