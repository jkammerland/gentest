# Failure diagnostics

A failing check prints its location and the values involved, formatted as
described in [Formatting values](value_formatting.md). This page shows how to
add context to a failure, check values with matchers, find the mock expectation
behind a failure, and use libassert's expression assertions. For the checks
themselves, see [Assertions](assertions.md).

Failure context and matchers come with `gentest/test.h` and with
`import gentest;`.

## Add context to failures

When a check runs in a loop or a helper, the failure alone doesn't say which
item failed. Open a context scope around the call:

```cpp
#pragma once

#include "gentest/test.h"

#include <vector>

namespace packets {

struct Packet {
    int id;
    int size;
};

inline void check_payload(const Packet &packet) { gentest::expect(packet.size > 0, "packet has a payload"); }

[[gentest::test]]
inline void all_have_payloads() {
    const std::vector<Packet> packets{{1, 10}, {2, 0}};
    for (const auto &packet : packets) {
        auto context = gentest::diagnostic_context("packet", packet.id);
        check_payload(packet);
    }
}

} // namespace packets
```

The failure lists every open context, with the place where it was created:

```text
[ FAIL ] packets/all_have_payloads :: 1 issue(s) (0 ms)
EXPECT_TRUE  failed at tests/packets.hpp:14: packet has a payload
  context: packet=2 (tests/packets.hpp:20)
```

- `gentest::diagnostic_context(name, value)` adds `name=value`. The value is
  formatted when the scope is created. `gentest::diagnostic_context("label")`
  adds a label without a value.
- Keep the returned object in a local variable. It can't be copied or moved,
  and the context ends when the variable goes out of scope.
- Nested scopes print from outer to inner. Leaving a scope, including through an
  exception, restores the enclosing context.
- The context lines are part of the failure text, so they also appear in the
  end-of-run summary, in JUnit and Allure reports and in GitHub annotations.
- Context is attached when a failure is recorded. An exception that escapes the
  case is reported after its scopes have closed, so it has no context.

In coroutine tests and with threads:

- A coroutine keeps its context across suspension.
- A child coroutine inherits a snapshot of its parent's context when it is first
  attached to the parent. Scopes that concurrent children create stay separate
  from their siblings and from the parent.
- Destructors that run when a coroutine is cancelled see the scopes that were
  open at that point.
- A worker thread that [adopts](logging_and_threads.md) the test context can't
  open a context scope or run checks. Return the worker's data to the case and
  check it there.

## Check values with matchers

A matcher describes the values it accepts and explains a mismatch:

```cpp
#pragma once

#include "gentest/test.h"

#include <string>
#include <vector>

using namespace gentest::asserts;

namespace matchers {

[[gentest::test]]
inline void messages_and_values() {
    using namespace gentest::match;
    const std::string message = "error: timeout after 3 retries";
    const std::vector<int> values{1, 2, 3};

    gentest::expect_that(message, AllOf(StartsWith("error:"), StrContains("timeout")));
    gentest::expect_that(values, ElementsAre(1, Ge(2), 3));
    gentest::require_that(values, Not(ElementsAre()));
    EXPECT_THAT(values.size(), InRange(1u, 5u), "batch size");
}

} // namespace matchers
```

`gentest::expect_that(value, matcher)` (`EXPECT_THAT`) records a failure and
lets the case continue; `gentest::require_that(value, matcher)` (`ASSERT_THAT`)
stops the case. Both take an optional message. A failure shows the
value and the matcher's explanation. If `values` were `{1, 0, 3}`, the run would
print:

```text
[ FAIL ] matchers/messages_and_values :: 1 issue(s) (0 ms)
EXPECT_THAT  failed at tests/matchers.hpp:19: actual=[1, 0, 3]; element[1]: expected >= 2, got 0
```

The matchers are in `gentest::match`:

| Matcher | Matches a value that |
|---|---|
| `Any()` | is anything |
| `Eq(x)` | equals `x` |
| `Ge(x)`, `Gt(x)`, `Le(x)`, `Lt(x)` | is `>= x`, `> x`, `<= x` or `< x` |
| `InRange(lo, hi)` | is in the closed range `[lo, hi]` |
| `Near(x, eps)` | differs from `x` by at most `eps` |
| `StartsWith(s)`, `EndsWith(s)`, `StrContains(s)` | is a string with that prefix, suffix or substring; a null character pointer never matches |
| `AllOf(m...)` | matches every matcher, checked in order up to the first mismatch; `AllOf()` matches everything |
| `AnyOf(m...)` | matches at least one matcher, checked in order up to the first match; `AnyOf()` matches nothing |
| `Not(m)` | doesn't match `m` |
| `ElementsAre(e...)` | is a forward range whose elements match `e...` in order |

Each argument of `ElementsAre` is a value (compared with `==`), a matcher or a
predicate. A mismatch reports the first failing index, a missing element or an
extra element. `ElementsAre()` matches an empty range.

The same matchers work in the `.where(...)` argument matchers of gentest mocks;
see [Mocks](mock_generation.md). A check or a mock call evaluates each matcher
once.

### Write your own matcher

A matcher can also be:

- a callable that takes the value and returns `bool`; its mismatch explanation
  is `predicate mismatch`;
- a `gentest::match::ArgPredicate<T>` with a `test` callback and an optional
  `describe` callback that explains a mismatch, or with an `evaluate` callback
  that returns a `gentest::match::MatchResult` (`matched` and `explanation`) in
  one call;
- a factory whose `make<T>()` member returns an `ArgPredicate<T>`.

```cpp
#pragma once

#include "gentest/test.h"

#include <string>

namespace matchers {

[[gentest::test]]
inline void custom_matchers() {
    gentest::expect_that(4, [](const int &value) { return value % 2 == 0; });

    gentest::match::ArgPredicate<int> even;
    even.evaluate = [](const int &value) -> gentest::match::MatchResult {
        if (value % 2 == 0)
            return {};
        return {.matched = false, .explanation = std::to_string(value) + " is odd"};
    };
    gentest::expect_that(6, even);
}

} // namespace matchers
```

## Mock failure locations

Expectations that you set on gentest mocks with `gentest::expect(...)` or
`EXPECT_CALL` remember where you wrote them. A missing call, an argument
mismatch, an extra call beyond an expectation, or a rejected change to an
expectation adds `(expectation at <file>:<line>)` to the failure. A call that
matches no expectation has no location to report. See
[Mocks](mock_generation.md) for setting up expectations.

## Expression assertions with libassert

To get libassert's expression breakdown in failure messages, include
`gentest/assert_libassert.h` in your test header and link libassert:

```cmake
find_package(libassert CONFIG REQUIRED)
target_link_libraries(my_tests PRIVATE gentest::gentest_main libassert::assert)
```

```cpp
#pragma once

#include "gentest/assert_libassert.h"

namespace expressions {

[[gentest::test]]
inline void sums() {
    const int total = 2 + 2;
    EXPECT_EQ(total, 4);
    ASSERT(total > 0);
}

} // namespace expressions
```

- The header installs a libassert failure handler that records failures in the
  current case. libassert's own macros, such as `ASSERT(...)`, stop the case.
- It also defines `EXPECT(expr)`, `EXPECT_EQ(a, b)` and `EXPECT_NE(a, b)`, which
  record a failure and continue, and `ASSERT_EQ(a, b)` and `ASSERT_NE(a, b)`,
  which stop the case. These macros hide gentest's functions of the same names.
  Define `GENTEST_NO_EXPECT_MACROS` or `GENTEST_NO_ASSERT_EQ_MACROS` before the
  include to leave them out.
- gentest's own checks and matchers don't need libassert. To build gentest's
  libassert integration tests, configure gentest with
  `-DGENTEST_WITH_LIBASSERT=ON`.
