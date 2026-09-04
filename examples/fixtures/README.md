# Fixtures without ordering dependencies

`Cart` is inferred from each free function's parameter and allocated afresh for
each case. `Catalog` is a suite fixture: its setup initializes shared data once
per run, and teardown releases it. It lives in the ancestor namespace of the
`checkout` cases. The tests read the catalog without mutating it.

There are three cases. `fresh_cart` and `add_item` mutate their local cart;
`empty_cart` still receives an empty one. Each case also works alone, and no
assertion relies on another case running first or on a fixed total run count.
CTest discovery runs individual cases in separate processes, so it does not
share catalog instances between those processes.

Fixture annotations belong on types, and test annotations belong on free
functions. A local fixture needs no lifetime annotation. See
[fixture allocation](../../docs/fixtures_allocation.md) for global lifetimes,
custom allocation, and allocation failures.

Build against an installed gentest package (CMake 3.31+, Ninja, C++20, and a
working host codegen tool):

```sh
cmake -S examples/fixtures -B build/examples/fixtures -G Ninja \
  -DCMAKE_PREFIX_PATH=/path/to/gentest/install
cmake --build build/examples/fixtures
ctest --test-dir build/examples/fixtures --output-on-failure

./build/examples/fixtures/gentest_fixtures --list-tests
./build/examples/fixtures/gentest_fixtures --repeat=2 --shuffle --seed 123
```

[`expected_tests.txt`](expected_tests.txt) records the exact resolved case names
checked by the repository's installed-package smoke test. All cases pass by
default. See the [examples index](../README.md) for installation links.
