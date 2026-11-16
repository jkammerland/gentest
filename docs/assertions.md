# Assertions

Checks report failures from inside a case. This page covers value checks,
exception checks, explicit failures, skips and expected failures. To add context
to a failure or to check values with matchers, see
[Failure diagnostics](failure_diagnostics.md).

## Check values

```cpp
#pragma once

#include "gentest/test.h"

#include <string>
#include <vector>

using namespace gentest::asserts;

namespace palette {

[[gentest::test]]
inline void colors() {
    const std::vector<std::string> colors{"red", "green"};
    ASSERT_EQ(colors.size(), 2u, "need two colors before indexing");
    EXPECT_EQ(colors[0], "red");
    EXPECT_NE(colors[1], "blue");
    EXPECT_TRUE(colors[1].starts_with("gr"));
}

} // namespace palette
```

An `EXPECT_*` check records a failure and lets the case continue. An `ASSERT_*`
check records a failure and stops the case, and the runner moves on to the next
case. A failure names the check, its location, your message if any and, for
comparisons, both values. If the first color were `"blue"`, the run would print:

```text
[ FAIL ] palette/colors :: 1 issue(s) (0 ms)
EXPECT_EQ    failed at tests/palette.hpp:16: lhs="blue", rhs="red"; first difference at line 1, column 1 (byte 0): 'b' versus 'r'
```

| Passes when | Continues on failure | Stops the case on failure |
|---|---|---|
| `c` is true | `EXPECT_TRUE(c)` | `ASSERT_TRUE(c)` |
| `c` is false | `EXPECT_FALSE(c)` | `ASSERT_FALSE(c)` |
| `a == b` | `EXPECT_EQ(a, b)` | `ASSERT_EQ(a, b)` |
| `a != b` | `EXPECT_NE(a, b)` | `ASSERT_NE(a, b)` |
| `a < b` | `EXPECT_LT(a, b)` | `ASSERT_LT(a, b)` |
| `a <= b` | `EXPECT_LE(a, b)` | `ASSERT_LE(a, b)` |
| `a > b` | `EXPECT_GT(a, b)` | `ASSERT_GT(a, b)` |
| `a >= b` | `EXPECT_GE(a, b)` | `ASSERT_GE(a, b)` |
| matcher `m` accepts `v` | `EXPECT_THAT(v, m)` | `ASSERT_THAT(v, m)` |

Every check takes an optional message as its last argument (a
`std::string_view`) and records the caller's file and line by itself.
Comparison failures print both operands as described in
[Formatting values](value_formatting.md), and point at the first difference in
strings and ranges.

The checks are functions in `gentest::asserts`, not macros, so each argument
(including the message) is evaluated exactly once, even when the check passes.
They work the same with `#include` and with `import gentest;`.

Each check also has a lowercase name in `gentest`. `EXPECT_EQ` is
`gentest::expect_eq` and `ASSERT_EQ` is `gentest::require_eq`, and likewise for
`NE`, `LT`, `LE`, `GT`, `GE` and `THAT`. The others are:

| Uppercase | Lowercase |
|---|---|
| `EXPECT_TRUE` | `gentest::expect` or `gentest::expect_true` |
| `EXPECT_FALSE` | `gentest::expect_false` |
| `ASSERT_TRUE` | `gentest::require` or `gentest::assert_true` |
| `ASSERT_FALSE` | `gentest::require_false` or `gentest::assert_false` |

`gentest::assert_eq` is another name for `gentest::require_eq`.

### Compare floating-point values

`gentest::approx::Approx` compares a value with a tolerance. Use it on either
side of `EXPECT_EQ`, `EXPECT_NE`, `ASSERT_EQ` or `ASSERT_NE`:

```cpp
#pragma once

#include "gentest/test.h"

using namespace gentest::asserts;

namespace geometry {

[[gentest::test]]
inline void circle_area() {
    using gentest::approx::Approx;
    const double area = 3.14159 * 1.0 * 1.0;
    EXPECT_EQ(area, Approx(3.14).abs(0.01));  // within 0.01
    EXPECT_EQ(area, Approx(3.14).rel(0.1));   // within 0.1 percent
}

} // namespace geometry
```

- `.abs(e)` accepts values within `e` of the target. `.rel(p)` accepts values
  within `p` percent of the larger of the two magnitudes. With both, either one
  is enough.
- Without a tolerance, `Approx` matches only equal values. Equal values always
  match, including `0.0` and `-0.0` and infinities of the same sign.
- NaN never matches, and an infinity matches only an equal infinity.
- A negative tolerance counts as its absolute value.

A failure prints the target and tolerances, for example
`Approx(target=1, abs=0.25, rel=2%)`.

## Fail explicitly

`gentest::fail("message")` records a failure and stops the case, for paths that
should never run. The failure reads `FAIL() :: message`.

## Exceptions

If an exception escapes a case, the runner records a failure and moves on to
the next case. A `std::exception` is reported as
`unexpected std::exception: <what()>`; any other type as `unknown exception`.

### Check that code throws

With `#include`, the exception checks are macros that take a statement:

```cpp
#pragma once

#include "gentest/test.h"

#include <stdexcept>
#include <vector>

using namespace gentest::asserts;

namespace exceptions {

[[gentest::test]]
inline void statement_macros() {
    std::vector<int> values{1, 2, 3};
    EXPECT_THROW((void)values.at(5), std::out_of_range);
    EXPECT_THROW(throw 123, int);
    EXPECT_NO_THROW(values.push_back(4); values.pop_back());
    ASSERT_NO_THROW((void)values.at(0));
}

} // namespace exceptions
```

| Check | Fails when |
|---|---|
| `EXPECT_THROW(statement, Type)` | the statement throws nothing, or something other than `Type` or a type derived from it |
| `ASSERT_THROW(statement, Type)` | the same, and stops the case |
| `EXPECT_NO_THROW(statement)` | the statement throws |
| `ASSERT_NO_THROW(statement)` | the same, and stops the case |

These three checks fail:

```cpp
EXPECT_THROW(throw std::invalid_argument("invalid"), std::runtime_error);
EXPECT_THROW((void)0, std::runtime_error);
EXPECT_NO_THROW(throw std::runtime_error("disk full"));
```

```text
EXPECT_THROW failed at tests/exceptions.hpp:13: expected std::runtime_error but caught std::exception: invalid
EXPECT_THROW failed at tests/exceptions.hpp:14: expected std::runtime_error but no exception was thrown
EXPECT_NO_THROW failed at tests/exceptions.hpp:15: caught std::exception: disk full
```

When the thrown value isn't a `std::exception`, the failure says
`caught unknown exception` instead.

With `import gentest;`, macros aren't available. Use the function templates of
the same names, which take a callable and call it:

```cpp
module;

#include <stdexcept>

export module my_tests.exceptions;

import gentest;

using namespace gentest::asserts;

export namespace exceptions {

[[using gentest: test("callables")]]
void callables() {
    EXPECT_THROW<std::runtime_error>([] { throw std::runtime_error("boom"); });
    EXPECT_NO_THROW([] {});
}

} // namespace exceptions
```

Rules for the two forms:

- A macro's first argument is a statement that the macro runs, not a callable.
  It can hold several statements separated by `;`.
- With `#include`, the macros hide the function templates of the same names.
  Passing a lambda-expression to a macro is a compile-time error, because the
  lambda would be created and never called. Call it in the statement
  (`EXPECT_NO_THROW(fn())`), or reach the function template by putting its name
  in parentheses: `(EXPECT_NO_THROW)(fn)`. `EXPECT_THROW<E>(fn)` and
  `ASSERT_THROW<E>(fn)` already reach the function template.
- A named callable passed to a macro, as in `EXPECT_NO_THROW(fn)`, is an
  expression statement that does nothing. The compiler reports it with an
  unused-value warning (`-Wunused-value`, which GCC turns on with `-Wall`).
- Define `GENTEST_NO_THROW_MACROS` before including gentest to drop the macros
  and use only the function templates.
- The macros print the exception type as you wrote it. The function templates
  print the compiler's `typeid` name, which GCC and Clang mangle (for example
  `St13runtime_error`), or `exception` when RTTI is off.
- gentest's own control flow passes through these checks. A failing `ASSERT_*`
  check, `gentest::fail()` or `gentest::skip()` inside the statement keeps its
  effect and isn't reported as a thrown exception, unless you name its type
  (`gentest::assertion` or `gentest::failure`) as the expected type.

### Check an exception's details

To check the message or other details, catch the exception yourself:

```cpp
#pragma once

#include "gentest/test.h"

#include <exception>
#include <stdexcept>
#include <string_view>

namespace exceptions {

inline void open_config() { throw std::runtime_error("boom: missing file"); }

[[gentest::test]]
inline void reports_missing_file() {
    try {
        open_config();
    } catch (const std::exception &e) {
        gentest::expect(std::string_view(e.what()).starts_with("boom"), "message starts with 'boom'");
        return;
    }
    gentest::fail("expected open_config() to throw");
}

} // namespace exceptions
```

Call `gentest::fail()` after the `try` block, not inside it. It throws
`gentest::failure`, which derives from `std::runtime_error`, so the
`catch (const std::exception &)` would catch it.

### How a fatal check stops a case

A failing `ASSERT_*` check records the failure and throws `gentest::assertion`.
`gentest::skip()` throws an internal type. Neither derives from
`std::exception`, so a `catch (const std::exception &)` in your code lets them
through, and destructors run as usual. A `catch (...)` catches them too, so
rethrow what you don't handle.

In a build without exceptions (`-fno-exceptions`), a failing `ASSERT_*` check
and `gentest::fail()` end the process with `std::terminate()`; run such cases as
[death tests](death_tests.md). `gentest::skip()` reports a skip, but the case's
local objects aren't destroyed and its local fixtures aren't torn down.
In that build `EXPECT_THROW` records
`exceptions are disabled; cannot verify thrown exception` without running the
statement, `ASSERT_THROW` ends the process, and the `*_NO_THROW` checks just run
the statement.

## Analyzer assertions

Flow-sensitive static analysis, such as clang-tidy's
`bugprone-unchecked-optional-access` check, can't tell that the `ASSERT_TRUE`
function stops the case, so it warns about code after the check. With
`#include`, you can opt into `ASSERT_TRUE` and `ASSERT_FALSE` macros that
analyzers understand:

```cpp
#pragma once

#include "gentest/analyzer_assertions.h"
#include "gentest/test.h"

#include <optional>

namespace lookup {

inline std::optional<int> find_answer() { return 42; }

[[gentest::test]]
inline void finds_answer() {
    const std::optional<int> result = find_answer();
    ASSERT_TRUE(result.has_value(), "the answer must exist");
    gentest::expect_eq(*result, 42);
}

} // namespace lookup
```

- The macros take a condition and an optional message, and fail the same way
  as the functions. The condition is evaluated once; the message only when the
  check fails.
- After the include, `ASSERT_TRUE` and `ASSERT_FALSE` are these macros instead
  of the `gentest::asserts` functions. The other checks and `gentest::require`
  stay functions.
- `import gentest;` can't export macros, so the header works with `#include`
  only.
- The header refuses to replace another framework's macros. If `ASSERT_TRUE` or
  `ASSERT_FALSE` is already defined, it stops with
  `gentest/analyzer_assertions.h requires ASSERT_TRUE and ASSERT_FALSE to be undefined`.

## Skip and expected failures

```cpp
#pragma once

#include "gentest/test.h"

using namespace gentest::asserts;

namespace outcomes {

inline bool has_gpu() { return false; }

[[gentest::test]]
inline void renders_on_gpu() {
    gentest::skip_if(!has_gpu(), "no GPU on this machine");
    EXPECT_TRUE(has_gpu());
}

[[gentest::test]]
inline void rounds_half_up() {
    gentest::xfail("BUG-123: rounding is off by one");
    EXPECT_EQ(2 + 2, 5);
}

} // namespace outcomes
```

```text
[ SKIP ] outcomes/renders_on_gpu :: no GPU on this machine (0 ms)
[ XFAIL ] outcomes/rounds_half_up :: BUG-123: rounding is off by one (0 ms)
```

| Call | Effect |
|---|---|
| `gentest::skip("reason")` | Stops the case and reports it as skipped. |
| `gentest::skip_if(condition, "reason")` | Skips when `condition` is true. |
| `gentest::xfail("reason")` | Marks the case as an expected failure and lets it continue. If it then fails or throws, it is reported as XFAIL. If it passes, it is reported as XPASS, which counts as a failure. |
| `gentest::xfail_if(condition, "reason")` | Calls `xfail` when `condition` is true. |

The reason is optional in all four. A skip doesn't hide a failure recorded
before it: the case is reported as failed. A skip after `xfail` wins, and the
case is reported as skipped. The end-of-run summary counts XFAIL cases as
skipped too. [Reports](reports.md) shows how each outcome appears in JUnit and
Allure reports.

### Static skip

A static skip is a case with the `skip` attribute:

```cpp
#pragma once

#include "gentest/test.h"

namespace outcomes {

[[using gentest: test("vendor_driver"), skip("needs the vendor driver")]]
inline void vendor_driver() {}

} // namespace outcomes
```

The runner reports it as skipped without running its body or setting up its
fixtures. It appears in listings like any other case: `--list` shows
`skip=needs the vendor driver`, and `--list-json` sets `skipped` and
`skipReason`. The reason is optional. With `gentest_discover_tests()`, CTest
shows static and runtime skips as skipped, and XFAIL cases as passed.

## Where checks can run

Checks, `gentest::fail()`, `skip` and `xfail` belong to the case's own thread or
coroutine. A worker thread that [adopts](logging_and_threads.md) the test
context may log, but a check there aborts the process with
`gentest: fatal: assertion/expectation called from an adopted test context.`
Pass the worker's results back to the case and check them there.
[Logging and threads](logging_and_threads.md) lists what a worker may call.
