# Benchmark, jitter, and template matrices

The same sum operation is instantiated for two accumulator types (`int`, `long`)
and two traversal directions (`false`, `true`). Each of three case declarations
expands into four cases: four correctness tests, four benchmarks, and four jitter
cases. See [expected_tests.txt](expected_tests.txt) for all 12 resolved names.

The correctness tests cover empty, single-element, and 64-element inputs.
Benchmarks and jitter cases use the same 64-element input. Fixture setup fills
that input outside timing; the timed body contains the sum and compiler barriers.
`items_per_call(64)` reports time per element, alongside sample/call information.
`clobberMemory()` prevents reusing previously loaded input, and
`doNotOptimizeAway()` keeps the result observable to the compiler.

Build against an installed gentest package with optimization enabled:

```sh
cmake -S examples/measured -B build/examples/measured -G Ninja \
  -DCMAKE_PREFIX_PATH=/path/to/gentest/install -DCMAKE_BUILD_TYPE=Release
cmake --build build/examples/measured
ctest --test-dir build/examples/measured --output-on-failure

./build/examples/measured/gentest_measured --kind=test
./build/examples/measured/gentest_measured --kind=bench --bench-table
./build/examples/measured/gentest_measured --kind=jitter --jitter-bins=20
```

CTest uses short smoke settings to check execution. For useful measurements,
run the executable directly with explicit settings appropriate to your workload:

```sh
./build/examples/measured/gentest_measured --kind=bench \
  --bench-epochs=20 --bench-warmup=5 --bench-min-epoch-time-s=0.01 \
  --bench-min-total-time-s=0.2 --bench-max-total-time-s=1 \
  --report-format=json > bench.json
./build/examples/measured/gentest_measured --kind=jitter \
  --bench-epochs=20 --bench-warmup=5 --bench-min-epoch-time-s=0.01 \
  --bench-min-total-time-s=0.2 --bench-max-total-time-s=1 \
  --report-format=json > jitter.json
./build/examples/measured/gentest_measured --list-json > inventory.json
```

The `--bench-*` calibration controls apply to both measured kinds. Machine reports
require a measured-only selection: omitting `--kind` here also selects correctness
tests and is rejected. Join report rows' `benchmark` names to inventory `name`
fields to recover declared owner, requirements, and template expansion identity.
The correctness cases declare `SUM-001`; all cases declare owner `examples`.

Timing values are not pass/fail criteria in CI. Keep compiler flags, host CPU,
power settings, load, and calibration settings alongside saved reports when
comparing runs. The two traversal directions may optimize identically, and `long`
has different widths across platforms; neither axis promises a speed difference.
This example measures a warm, small input, not cold-cache or contention behavior.
See the [metadata example](../metadata/) for the available metadata hooks and
[examples index](../README.md) for installation instructions.
