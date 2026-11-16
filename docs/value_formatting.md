# Formatting values

Failure messages print values with `gentest::format_value`. This page shows how
to make your own types print well, how standard types print, and how a failed
comparison points at the first difference.

## Make your type printable

A type that gentest can't print shows up in failures as its type name followed
by `(unprintable)`. Give it an `operator<<` in its namespace, or a
[fmt formatter](#use-a-fmt-formatter), to print it your way:

```cpp
#pragma once

#include "gentest/test.h"

#include <ostream>

using namespace gentest::asserts;

namespace geometry {

struct Point {
    int x;
    int y;
    bool operator==(const Point &) const = default;
};

inline std::ostream &operator<<(std::ostream &out, const Point &point) {
    return out << '(' << point.x << ", " << point.y << ')';
}

inline Point move_right(Point point) { return {point.x + 1, point.y}; }

[[gentest::test]]
inline void moves_right() {
    EXPECT_EQ(move_right(Point{1, 2}), Point{2, 2});
}

} // namespace geometry
```

If `move_right` returned its input unchanged, the failure would print both
points:

```text
EXPECT_EQ    failed at tests/geometry.hpp:25: lhs=(1, 2), rhs=(2, 2)
```

Without the `operator<<`, the same failure would print
`lhs=N8geometry5PointE (unprintable), rhs=N8geometry5PointE (unprintable)`.

`gentest::format_value(value)` returns this text as a `std::string` for your own
messages and logs. It is declared in `gentest/format_value.h`, which
`gentest/test.h` and `gentest/runner.h` include, and comes with `import gentest;`.

## How values are printed

`gentest::format_value` uses the first rule that fits the value:

1. Strings and characters are quoted. Newlines, carriage returns, tabs, NUL
   bytes, quotes, backslashes and other control bytes are escaped, as in
   `"tab\there\x1b"`.
   - This covers `std::string`, `std::string_view`, other types that convert to
     `std::string_view`, character arrays and character pointers.
   - A null character pointer prints as `nullptr`. Any other character pointer
     must point to a NUL-terminated string.
   - A character array ends at its first NUL, or at its full length, so
     `char buffer[8] = "ab"` prints as `"ab"`.
   - `signed char`, `unsigned char` and `std::byte` values print as numbers.
   - Pointers to `signed char` or `unsigned char`, such as `std::uint8_t *`,
     print as an address, or `nullptr` when null. gentest never reads through
     them.
2. A value with `operator<<` is written to a `std::ostringstream`. `bool` prints
   as `true` or `false`. Floating-point values print with `max_digits10` digits,
   so different values never print the same (`0.1` prints as
   `0.10000000000000001`). Function pointers print as `true`, or `false` when
   null.
3. Otherwise, a value that `fmt` can format uses fmt's default `{}` format.
4. Standard optionals, variants, tuples, pairs and forward ranges are printed
   element by element:

   | Value | Printed as |
   |---|---|
   | `std::optional<int>{3}` | `optional(3)` |
   | `std::optional<int>{}` | `nullopt` |
   | `std::variant<int, std::string>{"x"}` | `variant[1]("x")` |
   | `std::monostate{}` | `monostate` |
   | `std::tuple{1, 'c'}` | `(1, 'c')` |
   | `std::vector{1, 2, 3}` | `[1, 2, 3]` |
   | `std::map<std::string, int>{{"a", 1}}` | `[("a", 1)]` |

   A variant that is valueless by exception prints as `variant(valueless)`.
5. Anything else prints as its RTTI type name followed by ` (unprintable)`.
   GCC and Clang mangle that name, as in `N8geometry5PointE (unprintable)`.
   Without RTTI the text is `(unprintable, enable RTTI)`.

`nullptr` prints as `nullptr`. Output is bounded: a range stops after 32
elements with `...`, a string after 512 bytes with `... (<n> bytes)`, and
nesting after eight levels with `... (depth limit)`. Exceptions thrown by an
`operator<<` or a formatter reach the caller.

Value checks, matcher failures and gentest mock failures all use these rules.
One exception: matcher mismatch descriptions print function pointers as `1` or
`0`. Checking a null function pointer with `EXPECT_THAT(callback, Eq(&handler))`
reports `actual=false; expected == 1, got 0`. An `Approx` value prints its
target and tolerances, as in `Approx(target=1, abs=0.25, rel=2%)`.

## How differences are shown

When a comparison check fails on two strings or two forward ranges, the failure
also points at the first difference:

```text
lhs=[1, 2, 3], rhs=[1, 9, 3]; first difference at index 1: 2 versus 9
lhs="a\nb\0", rhs="a\nc\0"; first difference at line 2, column 1 (byte 2): 'b' versus 'c'
```

- When one side is shorter, the missing element shows as `<end>`, as in
  `first difference at index 2: <end> versus 3`.
- Indices and byte offsets start at 0. Line and column numbers start at 1 and
  count bytes.
- `==` on two character pointers compares addresses. When two different
  pointers point to equal strings, the failure says
  `operands are distinct pointers to equal strings (character pointers compare by address; compare std::string_view values to compare contents)`.
- The search for a difference looks at up to 4096 elements. Single-pass input
  ranges are never read for diagnostics.

## Use a fmt formatter

`operator<<` takes precedence over a `fmt::formatter`. A type that should print
through its fmt formatter must not also have an `operator<<`:

```cpp
#pragma once

#include "gentest/test.h"

#include <fmt/format.h>

#include <string>
#include <string_view>

namespace geometry {

struct Coordinate {
    int x;
    int y;
};

} // namespace geometry

template <> struct fmt::formatter<geometry::Coordinate> : fmt::formatter<std::string_view> {
    auto format(const geometry::Coordinate &value, fmt::format_context &ctx) const {
        return fmt::format_to(ctx.out(), "({}, {})", value.x, value.y);
    }
};

namespace geometry {

[[gentest::test]]
inline void prints_with_fmt() {
    gentest::expect_eq(gentest::format_value(Coordinate{3, 4}), std::string("(3, 4)"));
}

} // namespace geometry
```
