# Formatting values

`gentest::format_value(value)` returns Gentest's public, assertion-compatible human-readable value form. Include the narrow public
header for include-based code:

```cpp
#include "gentest/format_value.h"

std::string text = gentest::format_value(value);
```

Module consumers get the same API from `import gentest;`.

## Formatting order

Gentest keeps its established diagnostic rendering order:

1. A value with `operator<<` is written to an `std::ostringstream`. Boolean values use `std::boolalpha`.
2. Otherwise, a value accepted by `fmt` is formatted with the default `{}` presentation.
3. A value with neither representation uses its RTTI type name followed by ` (unprintable)`. When RTTI is disabled, the result is
   `(unprintable, enable RTTI)`.

Stream insertion stays ahead of `fmt` so existing diagnostic strings do not change. Assertions call `format_value` directly. Mock
diagnostics share the stream/`fmt` selection while retaining their established stream flags and raw type-name terminal fallback.

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
