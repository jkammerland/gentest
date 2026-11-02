# Failure diagnostics

Include `gentest/test.h` for ordinary tests, `gentest/runner.h` for async tests, or use `import gentest;`.
Comparisons print [standard values and first mismatches](value_formatting.md). Add scoped context when the values alone do not
identify a loop iteration or helper call.

## Scoped context

```cpp
for (const auto &packet : packets) {
    auto context = gentest::diagnostic_context("packet", packet.id);
    check_packet(packet);
}
```

Assertion failures recorded by `check_packet` include `context: packet=...` and the context declaration's source location. Context
values are formatted when the scope is created. Nested scopes appear in outer-to-inner order; scope exit and exception unwinding
restore the enclosing context. Successful assertions do not print context. JUnit, Allure, and recorded failure text include the same
context as console diagnostics.

For a label without a value, use `auto context = gentest::diagnostic_context("decoding header");`. Keep the returned object in its
lexical scope; it cannot be copied or moved. Context is attached when a failure is recorded, so assertions retain it before throwing
or unwinding; an exception reported after the scope has unwound has no retired lexical context.

A coroutine retains its context across suspension. Child coroutines inherit a snapshot when first attached to their parent;
scopes created in concurrent children stay isolated from siblings and the parent. Cancellation-time local destructors retain their
still-live scopes. Context creation belongs to the owning test/coroutine. Adopted workers may log and check stop; return data to the owner
and assert there, within the owner's diagnostic scope.

## Matcher assertions

`expect_that(value, matcher)` records a nonfatal failure. `require_that(value, matcher)` aborts the current case on mismatch.
Both accept an optional message and preserve the call site's source location. Uppercase `EXPECT_THAT` and `ASSERT_THAT` functions
are available in `gentest::asserts`.

```cpp
using namespace gentest::match;
gentest::expect_that(message, AllOf(StartsWith("error:"), StrContains("timeout")));
gentest::expect_that(values, ElementsAre(1, Ge(2), 3));
gentest::require_that(values, Not(ElementsAre()));
```

The shared `gentest/matchers.h` surface provides `Any`, `Eq`, `InRange`, `Ge`, `Le`, `Gt`, `Lt`, `Near`, `StartsWith`, `EndsWith`,
`StrContains`, `AllOf`, `AnyOf`, `Not`, and `ElementsAre`. `ElementsAre` accepts values, matcher factories, or predicates, and requires
a forward range. It reports missing/excess elements or a mismatch at a specific index. `AllOf` stops at the first mismatch;
`AnyOf` stops at the first match. Empty `AllOf` matches; empty `AnyOf` does not; empty `ElementsAre` matches an empty range.

The same matchers work in native mock `where_args(...)`. Assertions and mock dispatch evaluate each participating predicate once
per match. A custom matcher can be a callable returning `bool`, an `ArgPredicate<T>`, or a factory whose `make<T>()` returns one.
`gentest::match::ArgPredicate<T>` provides `test` and optional `describe` callbacks; its optional `evaluate` callback returns a
`gentest::match::MatchResult` with `matched` and `explanation` together.

## Native mocks and expression assertions

Native expectations capture their declaration locations through `gentest::expect(...)` and `EXPECT_CALL`. Missing calls, argument
mismatches, excess calls with an existing expectation, and rejected configuration identify that declaration. A call with no
configured expectation has no declaration location to report. See [mock generation](mock_generation.md) for configuration and backends.

For expression decomposition, include `gentest/assert_libassert.h` in your test translation unit and link the libassert dependency:

```cmake
find_package(libassert CONFIG REQUIRED)
target_link_libraries(my_tests PRIVATE gentest::gentest_main libassert::assert)
```

The adapter installs a libassert failure handler and provides expression-oriented assertion macros. Gentest's own integration tests
can be enabled with `GENTEST_WITH_LIBASSERT=ON`. The default assertions and matcher assertions do not require libassert.
