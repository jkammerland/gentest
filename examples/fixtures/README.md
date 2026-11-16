# Fixtures without ordering dependencies

This example shows the two fixture lifetimes you use most: a local fixture
that each case gets fresh, and a suite fixture that several cases share. Every
case passes on its own, in any order, and when repeated.

Build it against an installed gentest (see the [examples index](../README.md)
for requirements):

```bash
cmake -S examples/fixtures -B build/examples/fixtures -G Ninja -DCMAKE_PREFIX_PATH=/path/to/gentest/install
cmake --build build/examples/fixtures
ctest --test-dir build/examples/fixtures --output-on-failure
build/examples/fixtures/gentest_fixtures --list-tests
build/examples/fixtures/gentest_fixtures --repeat=2 --shuffle --seed 123
```

## What the cases show

[cases.hpp](cases.hpp) defines two fixtures:

- `Cart` has no fixture attribute, so it is a local fixture: every case that
  takes a `Cart &` parameter gets a fresh one.
- `Catalog` is a suite fixture (`[[gentest::fixture(suite)]]`) declared in the
  `fixtures` namespace, the common ancestor of the `checkout` cases. Those
  cases share one instance. It is set up when the first selected case that
  needs it runs, and torn down at the end of the run. The cases read the
  catalog without changing it.

There are three cases. `fresh_cart` and `add_item` change their local cart;
`empty_cart` receives an empty one anyway. No assertion relies on another case
running first or on a fixed number of runs. CTest runs each case in its own
process, so those processes don't share a catalog.

[expected_tests.txt](expected_tests.txt) lists the exact case names, which the
repository's installed-package test checks. All cases pass.

## Learn more

- [Fixtures](../../docs/fixtures.md): lifetimes, scoping to a namespace, setup
  and teardown, and global fixtures.
- [Fixture allocation hooks](../../docs/fixtures_allocation.md): custom
  allocation and allocation failures.
- [Examples](../README.md): installation links and the other examples.
