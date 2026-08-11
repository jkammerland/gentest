# Option 3: C++26 typed reflection registration

```cpp
#include <gentest/reflection.h>
#include <gentest/test.h>

namespace calculator_cases {

[[= gentest::reflection::test("calculator/addition")
        .tag("fast")
        .requirement("CALC-1")
        .owner("math-team")]]
void addition() {
    gentest::expect_true(2 + 2 == 4);
}

[[= gentest::reflection::bench("calculator/throughput")
        .baseline()
        .itemsPerCall(16)]]
void throughput() {
    // measured operation
}

} // namespace calculator_cases

// The explicit scope is required: standard reflection has no "this TU" query.
GENTEST_REFLECT_REGISTER(calculator_cases)
```

```cmake
add_executable(calculator_tests calculator_cases.cpp)
target_link_libraries(calculator_tests PRIVATE gentest::gentest_main)
gentest_enable_reflection(calculator_tests)
gentest_discover_tests(calculator_tests)

# No gentest_attach_codegen() call.
```

## Status

Experimental prototype for free, no-argument, synchronous `void` tests,
benchmarks, and jitters. The current implementation is documented in
`docs/reflection.md` and implemented by `include/gentest/reflection.h`.

This option can grow substantially without external code generation, but some
boundaries come from the final C++26 reflection surface rather than missing
implementation work.

## Extended user-facing design

Fixtures, value parameters, and async cases are feasible with typed annotations
and a backend-neutral adapter core:

```cpp
namespace calculator_cases {

struct [[= gentest::reflection::fixture(gentest::fixture_scope::suite)]] Database
    : gentest::FixtureSetup,
      gentest::FixtureTearDown {
    void setUp() override;
    void tearDown() override;
};

[[= gentest::reflection::test("calculator/query")]]
void query(
    Database &database,
    [[= gentest::reflection::values(
        gentest::reflection::arg("one", 1),
        gentest::reflection::arg("two", 2))]] int value) {
    gentest::expect_true(database.contains(value));
}

[[= gentest::reflection::test("calculator/async")]]
gentest::async_test<void> async_query(Database &database) {
    co_await database.ready();
}

} // namespace calculator_cases

GENTEST_REFLECT_REGISTER(calculator_cases)
```

This syntax is illustrative. Exact-size structural annotation types are
preferable to fixed-capacity buffers in a stable API:

```cpp
[[= gentest::test<"calculator/query">,
  = gentest::tags<"fast", "database">,
  = gentest::requirements<"CALC-1">,
  = gentest::owner<"math-team">]]
void query(Database &database);
```

Separate typed annotation categories avoid arbitrary string and item-count
limits and allow focused diagnostics.

## How reflected adapters work

For each discovered ordinary function, headers can:

```cpp
template<std::meta::info Function, std::size_t... Indices>
void invoke(std::index_sequence<Indices...>) {
    [:Function:](bind_argument<
        std::meta::parameters_of(Function)[Indices]>()...);
}
```

The implementation can:

1. Read typed case annotations.
2. Inspect parameter types and parameter annotations.
3. Expand structural value axes into wrapper specializations.
4. Treat remaining `T&`, `T*`, and `std::shared_ptr<T>` parameters as fixtures.
5. Read a fixture type's local/suite/global policy.
6. Inspect the function return type for `void` or
   `gentest::async_test<void>`.
7. Instantiate the same typed adapters used by option 2.
8. Register normal runtime `gentest::Case` records.

Reflection does not need to determine whether a body contains `co_await`.
Gentest's observable async contract is selected by the reflected return type.

## Namespace footer requirement

This is not implementable in standard C++26:

```cpp
#include <gentest/register_everything_from_this_cpp.inc>
```

There is no reflection representing an entire translation unit or linked
program, and no query for “declarations originating in this source file.”

`std::meta::members_of(namespace, context)` returns eligible reachable members
that precede the evaluation point. A global or shared namespace scan can
therefore include declarations from headers or imports, while declarations in
another ordinary `.cpp` remain invisible.

Use one dedicated namespace owned by the translation unit:

```cpp
namespace parser_case_tu {
// All cases owned by parser_cases.cpp.
}

GENTEST_REFLECT_REGISTER(parser_case_tu)
```

Direct members are visible. Nested namespaces either need recursive traversal
under a dedicated root or explicit registration:

```cpp
namespace parser_case_tu::fast { /* cases */ }
namespace parser_case_tu::slow { /* cases */ }

GENTEST_REFLECT_REGISTER(parser_case_tu::fast)
GENTEST_REFLECT_REGISTER(parser_case_tu::slow)
```

The footer must appear after every declaration it owns. A fresh internal-linkage
call-site tag must parameterize reflection helpers so an earlier template
instantiation cannot freeze an earlier view of an extensible namespace.

A header that registers a shared namespace can register the same cases once per
including translation unit. Treat registration footers as source-owned code,
not reusable header content.

## Function templates

Final C++26 reflection can represent a function template and can substitute
explicit reflection arguments, but it does not provide a general
`template_parameters_of` query. Function-template annotations also cannot be
consumed exactly like ordinary function annotations.

Template matrices therefore need an explicit descriptor at the footer:

```cpp
namespace matrix_case_tu {

template<class T, int N>
void matrix() {
    gentest::expect_true(N > 0);
}

} // namespace matrix_case_tu

GENTEST_REFLECT_REGISTER_TEMPLATE(
    "matrix/basic",
    ^^matrix_case_tu::matrix,
    gentest::reflection::type_axis<^^int, ^^long>,
    gentest::reflection::value_axis<1, 2>)
```

The explicit axes make `std::meta::substitute` form each specialization. This
is a real standard boundary, not merely missing work in the prototype.

## Parameter values

Pure reflection handles values that can be represented as compile-time
structural constants:

```cpp
void parse(
    [[= gentest::values<
        gentest::arg<"small", 1>,
        gentest::arg<"large", 100>>]] int value);
```

For non-structural objects, use reflected factories rather than embedding raw
objects in annotation values:

```cpp
Widget makeSmall();
Widget makeLarge();

void consume(
    [[= gentest::factories<^^makeSmall, ^^makeLarge>]] Widget widget);
```

This is intentionally less permissive than legacy codegen pasting arbitrary
source expressions into generated wrappers.

## Duplicate-name policy

Within one footer, expanded cases are known together and can be checked during
constant evaluation.

Across translation units, portable compile-time duplicate detection is not
available. Separate compilation prevents one reflection evaluation from seeing
other TUs. Computed names also cannot be turned portably into unique strong
linker symbols.

Registrars should collect cases and source locations. Before `--list` or
execution, the runner sorts and reports all duplicate origins. Registration
order must not affect output because cross-TU dynamic initialization order is
unspecified.

## Named modules

One module unit cannot discover non-reachable declarations in sibling units.
Each unit therefore needs an owned scope and footer:

```cpp
module;
#include <gentest/reflection.h>

export module calculator.tests;
import gentest;

namespace calculator_interface_cases {

[[= gentest::reflection::test("calculator/module")]]
void module_case() {
    gentest::expect_true(true);
}

} // namespace calculator_interface_cases

GENTEST_REFLECT_REGISTER(calculator_interface_cases)
```

A future `gentest.reflection` module can replace the global-module-fragment
include. Registration is usually better placed in private test implementation
units than reusable exported interfaces.

## Generated mocks are not possible with reflection alone

Final C++26 provides `std::meta::define_aggregate`, which defines aggregate
classes from non-static data-member descriptions. It does not synthesize base
classes, virtual overriding member functions, arbitrary constructors, or
function bodies.

Reflection alone therefore cannot turn:

```cpp
struct Service {
    virtual int compute(int) const = 0;
    virtual void reset() noexcept = 0;
};
```

into a substitutable `ServiceMock`. A reflection-driven proxy such as
`proxy.call<^^Service::compute>(3)` is not a `Service&` and does not override
virtual dispatch.

No-Clang choices remain explicit mock macros, handwritten fakes, a shared
method schema, or direct gMock/Trompeloeil use.

## Existing attributes are not source-compatible

Standard typed annotations expose values written with the `[[=...]]` syntax.
They do not reinterpret Gentest's current vendor attributes:

```cpp
[[using gentest: test("calculator/addition"), fast]]
void addition();
```

Moving to pure reflection requires a source migration or retaining the legacy
backend for those files.

## Feature gates

The implementation must check the required final SD-6 macro revisions for both
the language and `<meta>` library, then run a focused compile probe for the
exact annotation, member-query, splice, extraction, and source-location surface
Gentest uses.

```cpp
#if !defined(__cpp_impl_reflection) || __cpp_impl_reflection < 202603L
#error "Gentest reflection requires final C++26 core reflection"
#endif

#if !defined(__cpp_lib_reflection) || __cpp_lib_reflection < 202603L
#error "Gentest reflection requires the final C++26 <meta> library"
#endif
```

These values cover the final parameter-annotation/query surface used by the
extended design. The focused probe remains a secondary semantic gate because a
vendor may define the revision while an individual API is incomplete.

Do not treat `-std=c++26`, `__cplusplus`, or mere macro presence as proof of a
complete implementation. Vendor transitional flags such as GCC's
`-freflection` remain probe-selected implementation details.

The fallback path must remain includable in older language modes and diagnose
an attempted reflection registration clearly.

`std::meta::source_location_of` does not promise that every implementation
returns the exact authored owner path and line Gentest would prefer. Reflected
registration should preserve a usable location when provided, but must accept
empty, implementation-selected, or non-owner locations. Diagnostics and case
ordering need a deterministic fallback such as the registration footer's
`std::source_location`; exact source fidelity should be covered by a toolchain
probe rather than treated as a portable language guarantee.

## Current ecosystem constraint

At the time of this design, [GCC's status page](https://gcc.gnu.org/projects/cxx-status.html)
lists GCC 16 reflection behind `-freflection`, while
[Clang's published C++ status](https://clang.llvm.org/cxx_status.html) does not
list reflection as implemented. Consequently, reflection removes Gentest's
compilation-database manipulation but may give current clangd versions a source
syntax they cannot parse.

This makes reflection suitable as an opt-in frontend, not yet as Gentest's only
public declaration syntax.

## What works or is feasible

- typed test/bench/jitter metadata;
- explicit namespace-member discovery;
- ordinary free functions and best-available source locations with a defined
  footer fallback;
- local/suite/global fixtures through reflected parameter and type metadata;
- structural value parameters, rows, and numeric sequences;
- async tests through return-type inspection;
- benchmark/jitter setup-call-teardown adapters;
- explicit function-template matrices through substitution;
- compile-time duplicate checking within one footer;
- normal runtime registration and CTest `--list` discovery;
- named-module units with an owned scope/footer;
- no Gentest codegen or compilation-database dependency.

## What cannot be provided as stated

- existing `[[using gentest: ...]]` source compatibility;
- parameterless translation-unit discovery;
- transparent registration of arbitrary shared namespaces from headers;
- fully automatic function-template matrix discovery;
- arbitrary non-constant parameter expressions without factories;
- compile-time duplicate diagnostics across translation units;
- current generated mock classes;
- portability to compilers and language servers without the final reflection
  syntax and library.

## Validation requirements

- final macro-value and focused API probes;
- empty, implementation-selected, and exact reflected source-location results;
- positive reflection build and fallback include build;
- declarations before and after the footer;
- direct and nested namespace behavior;
- shared-header duplicate registration diagnostics;
- fixture argument forms and lifetime scoping;
- parameter empty/boundary/invalid annotations and factory failures;
- async success, failure, block, cancellation, and teardown;
- measured setup/call/teardown separation;
- template type/value axes and invalid substitutions;
- duplicate names within one footer and across TUs;
- `--list`, CTest discovery, and cross-build behavior;
- module interface and private implementation-unit ownership;
- install/consumer builds with codegen omitted.

## Standards sources

- [N5050, the final public C++26 draft](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2026/n5050.pdf)
- [typed annotation wording](https://eel.is/c++draft/dcl.attr.annotation)
- [`members_of` visibility and ordering](https://eel.is/c++draft/meta.reflection.member.queries)
- [`define_aggregate` generation boundary](https://eel.is/c++draft/meta.reflection.define.aggregate)

## Repository anchors

- [current reflection prototype](../../include/gentest/reflection.h)
- [current reflection user documentation](../reflection.md)
- [reflection prototype tests](../../tests/reflection_cases.cpp)
