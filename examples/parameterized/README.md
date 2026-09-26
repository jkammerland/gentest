# Parameterized cases

This example uses standard containers to show three ways to generate cases:

- `filledVector`: independent size/value axes, producing nine cases.
- `clampRows`: five input/expected rows, including both boundaries and out-of-range inputs.
- `pushPop`: two element types crossed with two container templates, producing four cases.

Empty containers and zero values are intentional. The empty-container predicate
is vacuously true; the separate size assertion verifies that case explicitly.
Rows describe paired inputs; separate axes describe a Cartesian product.

Build against an installed gentest package (CMake 3.31+, Ninja, C++20, and a
working host codegen tool):

```sh
cmake -S examples/parameterized -B build/examples/parameterized -G Ninja \
  -DCMAKE_PREFIX_PATH=/path/to/gentest/install
cmake --build build/examples/parameterized
ctest --test-dir build/examples/parameterized --output-on-failure

./build/examples/parameterized/gentest_parameterized --list-tests
./build/examples/parameterized/gentest_parameterized --repeat=2 --shuffle --seed 123
```

[`expected_tests.txt`](expected_tests.txt) records the exact resolved case names
checked by the repository's installed-package smoke test. All cases pass by
default. See the [examples index](../README.md) for installation links.
