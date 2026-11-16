# Parameters and templates

Write one test function and let codegen expand it into many cases: give its
parameters lists of values, or give a function template lists of template
arguments. Each combination becomes a case with its own name.

## Value parameters

```cpp
#pragma once

#include "gentest/test.h"

namespace params {

[[gentest::test("pairs")]]
[[gentest::parameters(a, 1, 2)]]
[[gentest::parameters(b, 10, 20)]]
inline void pairs(int a, int b) {
    gentest::expect((a == 1 || a == 2) && (b == 10 || b == 20), "values from axes");
}

} // namespace params
```

This declares four cases:

```text
params/pairs(1, 10)
params/pairs(1, 20)
params/pairs(2, 10)
params/pairs(2, 20)
```

`parameters(name, values...)` names one function parameter and lists its
values; this is called an axis. With several axes, codegen generates every
combination. A case's name is its base name followed by its values, in
parameter order. Quote such names for the shell:

```bash
./my_tests --run='params/pairs(1, 10)'
./my_tests --filter='params/pairs(*'
```

[examples/parameterized](../examples/parameterized) is a complete project with
axes, rows and templates.

### Rows instead of combinations

`parameters_pack` lists rows of values that belong together:

```cpp
#pragma once

#include "gentest/test.h"

namespace params {

[[using gentest: test("rows"), parameters_pack((a, b), (1, 10), (2, 20))]]
inline void rows(int a, int b) {
    gentest::expect((a == 1 && b == 10) || (a == 2 && b == 20), "row values");
}

} // namespace params
```

This declares two cases, `params/rows(1, 10)` and `params/rows(2, 20)`. The
first tuple names the parameters; every further tuple is one row with one value
per name. Several packs, and packs together with axes, combine as every
combination of their rows and values.

### How values are written

Codegen pastes each value into the generated call as a C++ expression, so the
value of a non-string parameter can be a literal, a constant or an expression
such as `std::chrono::milliseconds{10}` or `Point{1, 2}`. Names resolve as they would
inside the case's namespace. The case name shows the expression that codegen
passes, not its computed value: `parameters(limit, kLimit)` gives a name ending
in `(kLimit)`.

String and character parameters are quoted for you:

- For a string parameter (`std::string`, `std::string_view`, `const char*` and
  their wide and UTF variants), codegen turns every value that isn't a string
  literal into one. `parameters(s, hello, "world")` passes `"hello"` and
  `"world"`, and `parameters(s, kName)` passes the text `"kName"`, not the
  constant's value. Codegen adds the `L`, `u8`, `u` or `U` prefix the
  parameter type needs.
- For a character parameter, a single character becomes a character literal.
  `parameters(c, a, 'z')` passes `'a'` and `'z'`.

Codegen recognizes a string parameter by how its type is spelled. Any type
whose name contains `string` counts, so for a `std::optional<std::string>`
parameter `parameters(o, std::nullopt)` passes the text `"std::nullopt"`. A type
alias such as `using Name = std::string;` doesn't count. For every other
parameter, codegen removes the quotes around a quoted value: for a
`std::filesystem::path` parameter, write `std::filesystem::path("data.txt")`,
not `"data.txt"`.

### Generated number ranges

These attributes compute the values of one numeric axis:

| Attribute | Values | Example |
|---|---|---|
| `range(name, start, step, end)` | `start`, `start + step`, ... up to and including `end` | `range(i, 1, 2, 9)`: 1, 3, 5, 7, 9 |
| `range(name, "start:step:end")` | The same, written as one string | `range(i, "1:2:9")`: 1, 3, 5, 7, 9 |
| `linspace(name, start, end, count)` | `count` evenly spaced values from `start` to `end` | `linspace(x, 0.0, 1.0, 5)`: 0, 0.25, 0.5, 0.75, 1 |
| `geom(name, start, factor, count)` | `count` values, each `factor` times the one before | `geom(n, 1, 2, 5)`: 1, 2, 4, 8, 16 |
| `logspace(name, start, end, count)` or `logspace(name, start, end, count, base)` | `count` powers of `base` (10 by default), with exponents evenly spaced from `start` to `end` | `logspace(f, -3, 3, 7)`: 0.001, 0.01, ... 1000 |

`geomspace` and `geospace` are other names for `geom`.

Codegen computes these values while it generates code, so:

- The bounds, step, factor, count and base must be numeric literals. Decimal,
  hexadecimal, binary and octal integers, floating-point literals with
  exponents, suffixes and digit separators all work. A named constant or an
  expression is an error.
- The parameter must have an integer or floating-point type. For an integer
  parameter, `linspace`, `geom` and `logspace` round each value to the nearest
  integer.
- A step of 0 counts as 1, and a count below 1 counts as 1. A step that moves
  away from `end` is an error, and so is an axis with more than 1,000,000
  values.
- Floating-point values appear in names with up to 17 significant digits, so
  `logspace(f, -3, 3, 7)` includes a case ending in `(0.10000000000000001)`.

### What codegen checks

| Problem | Codegen message |
|---|---|
| An axis names no parameter of the function. | `unknown parameter name 'x' in parameters(...)`; for the other attributes the message names `parameters_pack(...)`, `parameters_range(...)` (for `range`), `parameters_linspace(...)`, `parameters_geom(...)` or `logspace(...)` |
| Two axes name the same parameter. | `duplicate parameter axis for 'x'` |
| A pack and another axis or pack name the same parameter. | `parameter 'x' supplied by multiple parameter sets/packs` |
| A row has the wrong number of values. | `'parameters_pack' value tuple arity mismatch` |

A parameter that no axis names follows the fixture rules: it keeps its default
argument if it has one, and is a fixture otherwise. Fixture and value
parameters can appear in any order. See [Fixtures](fixtures.md).

## Templates

`template(NAME, arguments...)` lists the arguments for one template parameter.
It works for type, value and template template parameters:

```cpp
#pragma once

#include "gentest/test.h"

#include <deque>
#include <vector>

namespace parameterized {

// Two element types x two container templates produce four concrete cases.
template <typename T, template <class...> class Container>
[[using gentest: test("push_pop"), template(T, int, long), template(Container, std::vector, std::deque)]]
void pushPop() {
    Container<T> values;
    gentest::expect_true(values.empty());
    values.push_back(T{7});
    gentest::expect_eq(values.back(), T{7});
    values.pop_back();
    gentest::expect_true(values.empty());
}

} // namespace parameterized
```

```text
parameterized/push_pop<int,std::deque>
parameterized/push_pop<int,std::vector>
parameterized/push_pop<long,std::deque>
parameterized/push_pop<long,std::vector>
```

- Give every template parameter its own `template(...)` attribute. Codegen
  reports a missing one as
  `missing 'template(T, ...)' attribute for template parameter 'T'`.
- A function template without any `template(...)` attribute is accepted only
  if each template parameter has a default argument, is a pack, or is deduced
  from the case's value parameters. Otherwise codegen reports
  `function template requires a 'template(T, ...)' attribute: template parameter 'T' has no default argument and cannot be deduced from the generated call`.
- Template arguments combine as every combination, also with value axes:
  `template(T, int, long)` together with `parameters(v, 2, 4)` gives four cases,
  such as `typed_values<int>(2)`.
- The name lists the template arguments in the order the template declares its
  parameters, separated by `,` without spaces, before any values.

### Template parameter packs

For a template parameter pack, write each set of arguments as a parenthesized
row in the same `template(...)` attribute; `()` is the empty pack:

```cpp
#pragma once

#include "gentest/test.h"

#include <list>
#include <string>
#include <tuple>
#include <vector>

namespace packs {

template <template <class...> class... Cs>
[[using gentest: template(Cs, (), (std::vector), (std::vector, std::list))]]
void tt_pack() {
    using tuple_t    = std::tuple<Cs<int>...>;
    const auto arity = std::tuple_size_v<tuple_t>;
    gentest::log(std::string("template-template pack arity = ") + std::to_string(arity));
    gentest::expect_eq(arity, sizeof...(Cs), "template-template pack arity");
}

} // namespace packs
```

This expands to three cases: `packs/tt_pack<>`, `packs/tt_pack<std::vector>`
and `packs/tt_pack<std::vector,std::list>`.

A pack needs parenthesized rows, and a row can't have an empty entry. A type or
template template parameter that isn't a pack doesn't accept a parenthesized
row.

### Fixture types from template parameters

A fixture parameter's type can be a template type parameter that `template(...)`
binds. Each case then gets a fixture of the bound type, with that type's
lifetime:

```cpp
#pragma once

#include "gentest/test.h"

#include <vector>

namespace buffers {

struct SmallBuffer {
    std::vector<int> data = std::vector<int>(4);
};

struct LargeBuffer {
    std::vector<int> data = std::vector<int>(1024);
};

template <typename Buffer>
[[using gentest: test("filled"), template(Buffer, SmallBuffer, LargeBuffer)]]
void filled(Buffer &buffer) {
    gentest::expect(!buffer.data.empty());
}

} // namespace buffers
```

This declares `buffers/filled<LargeBuffer>` and `buffers/filled<SmallBuffer>`.
The fixture type must be exactly the template parameter, not a pack and not
declared after a pack, and each argument must name a class, enum or type alias.
Codegen rejects other fixture types that depend on a template parameter.
