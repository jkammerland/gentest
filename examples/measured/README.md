# Benchmark, jitter and template matrices

This example checks one sum function for correctness and measures it, as a
benchmark and as a jitter case. A template matrix runs all three for two
accumulator types and two traversal directions.

Build it against an installed gentest with optimization on, then run each kind:

```bash
cmake -S examples/measured -B build/examples/measured -G Ninja -DCMAKE_PREFIX_PATH=/path/to/gentest/install -DCMAKE_BUILD_TYPE=Release
cmake --build build/examples/measured
ctest --test-dir build/examples/measured --output-on-failure
build/examples/measured/gentest_measured --kind=test
build/examples/measured/gentest_measured --kind=bench
build/examples/measured/gentest_measured --kind=jitter --jitter-bins=20
```

## What the cases show

[cases.hpp](cases.hpp) declares three cases, each with
`template(T, int, long)` and `template(Reverse, false, true)`. Each declaration
expands into four cases, one per accumulator type (`int`, `long`) and direction
(`false`, `true`): four correctness tests, four benchmarks and four jitter
cases. [expected_tests.txt](expected_tests.txt) lists all 12 names.

- The correctness tests sum an empty input, a single element and all 64
  elements.
- The benchmarks and jitter cases sum the same 64-element input. The `Input`
  fixture fills it in setup, outside the timed loop; the timed body holds only
  the sum and the compiler barriers.
- `items_per_call(64)` makes the reports show time per element, next to the
  sample and call counts.
- `clobberMemory()` stops the compiler from reusing loaded input, and
  `doNotOptimizeAway()` keeps the result observable.
- The correctness tests declare `req("SUM-001")`; every case declares
  `owner("examples")`.

## Measure for real

CTest uses short settings ([CMakeLists.txt](CMakeLists.txt) passes them through
`gentest_discover_tests(... EXTRA_ARGS ...)`) and only checks that the cases
run. For useful numbers, run the executable with settings that suit your
workload:

```bash
build/examples/measured/gentest_measured --kind=bench --bench-epochs=20 --bench-warmup=5 --bench-min-epoch-time-s=0.01 --bench-min-total-time-s=0.2 --bench-max-total-time-s=1 --report-format=json > bench.json
build/examples/measured/gentest_measured --kind=jitter --bench-epochs=20 --bench-warmup=5 --bench-min-epoch-time-s=0.01 --bench-min-total-time-s=0.2 --bench-max-total-time-s=1 --report-format=json > jitter.json
build/examples/measured/gentest_measured --list-json > inventory.json
```

The `--bench-*` settings apply to both measured kinds. A JSON or CSV report
needs a selection of measured cases only. Without `--kind`, the selection also
contains the correctness tests, and the runner rejects it:

```text
error: --report-format=json requires a measured-only selection; use --kind=bench, --kind=jitter, or a measured-only filter
```

To recover each row's owner, requirements and template arguments, join the
report rows' `benchmark` names with the inventory's `name` fields.

Timings are not pass or fail criteria. When you compare saved reports, record
the compiler flags, host CPU, power settings, machine load and calibration
settings with them. Neither axis promises a speed difference: the two
directions may compile to the same code, and the width of `long` varies by
platform. The input is small and warm, so the example doesn't measure cold
caches or contention.

## Learn more

- [Benchmarks and jitter](../../docs/benchmarks.md): measured cases, phases,
  report formats and comparing reports.
- [Metadata example](../metadata/): requirements, owners and tags.
- [Examples](../README.md): installation links and the other examples.
