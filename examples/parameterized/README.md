# Parameterized cases

This example generates 18 cases from three functions, using standard
containers. It shows three ways to turn one function into many cases.

Build it against an installed gentest (see the [examples index](../README.md)
for requirements):

```bash
cmake -S examples/parameterized -B build/examples/parameterized -G Ninja -DCMAKE_PREFIX_PATH=/path/to/gentest/install
cmake --build build/examples/parameterized
ctest --test-dir build/examples/parameterized --output-on-failure
build/examples/parameterized/gentest_parameterized --list-tests
build/examples/parameterized/gentest_parameterized --repeat=2 --shuffle --seed 123
```

## What the cases show

[cases.hpp](cases.hpp) has three functions:

| Function | Attributes | Cases |
| --- | --- | --- |
| `filledVector` | Two independent axes, `parameters(count, 0, 1, 4)` and `parameters(value, -1, 0, 1)` | 9, one per combination |
| `clampRows` | `parameters_pack((input, expected), ...)` with five rows, including both boundaries and out-of-range inputs | 5, one per row |
| `pushPop` | `template(T, int, long)` and `template(Container, std::vector, std::deque)` | 4, one per type and container |

Separate axes form a Cartesian product. Rows keep values that belong together,
such as an input and its expected result, in one case.

Empty containers and zero values are deliberate. For an empty container the
`std::all_of` check is trivially true, so a separate size assertion checks that
case explicitly.

[expected_tests.txt](expected_tests.txt) lists the exact case names, which the
repository's installed-package test checks. All cases pass.

For the other parameter helpers, such as value ranges, see
[Parameters and templates](../../docs/parameters.md).
