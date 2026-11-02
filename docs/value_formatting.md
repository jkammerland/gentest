# Formatting values

`gentest::format_value(value)` returns Gentest's public, assertion-compatible human-readable value form. Include the narrow public
header for include-based code:

```cpp
#include "gentest/format_value.h"

std::string text = gentest::format_value(value);
```

Module consumers get the same API from `import gentest;`.

## Formatting order

1. Narrow strings and characters are quoted, with visible escapes for newlines, tabs, NULs, quotes, backslashes, and control bytes.
   Null C string pointers print as `nullptr`.
   Signed/unsigned byte values and `std::byte` print as numbers.
2. A value with `operator<<` is written to an `std::ostringstream`. Booleans use `std::boolalpha`; floating-point values use
   `max_digits10` precision so adjacent unequal values remain distinguishable.
3. Otherwise, a value accepted by `fmt` uses the default `{}` presentation.
4. Standard optionals, variants, forward ranges, and tuples/pairs are formatted recursively. Maps print as ranges of key/value pairs.
5. A value with none of these representations uses its RTTI type name followed by ` (unprintable)`. When RTTI is disabled, the result is
   `(unprintable, enable RTTI)`.

Assertions and mock diagnostics share these representations. Function-pointer mock predicates retain their numeric boolean spelling.
An `Approx` value shows its target, absolute tolerance, and relative percent.

```text
lhs=[1, 2, 3], rhs=[1, 9, 3]; first difference at index 1: 2 versus 9
lhs="a\nb\0", rhs="a\nc\0"; first difference at line 2, column 1 (byte 2): 'b' versus 'c'
```

Comparison failures include the first differing forward-range element or string byte, including an explicit `<end>` for unequal
lengths. Sequence indices and byte offsets start at zero; string line/column positions start at one and count bytes.
Structural output is limited to 32 range elements, 512 string bytes, and eight
nested levels; sequence mismatch scanning examines up to 4096 positions. Single-pass input ranges are not consumed for diagnostics.

A type that should use its `fmt` customization should therefore omit stream insertion:

```cpp
struct Coordinate {
    int x;
    int y;
};

template <> struct fmt::formatter<Coordinate> : fmt::formatter<std::string_view> {
    auto format(const Coordinate &value, fmt::format_context &ctx) const {
        return fmt::format_to(ctx.out(), "({}, {})", value.x, value.y);
    }
};

auto text = gentest::format_value(Coordinate{3, 4}); // "(3, 4)"
```

Formatting and stream exceptions propagate to the caller.

For helper-call context and descriptive matcher assertions, see [failure diagnostics](failure_diagnostics.md).
