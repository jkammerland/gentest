# Runtime records

This example records properties and payloads while cases run, then exports them
next to a JUnit report. Three cases share the same input preparation: a
correctness test, a benchmark and a jitter case.

Build it against an installed gentest (see the [examples index](../README.md)
for requirements), then run it with records and a JUnit report:

```bash
cmake -S examples/recording -B build/examples/recording -G Ninja -DCMAKE_PREFIX_PATH=/path/to/gentest/install
cmake --build build/examples/recording
ctest --test-dir build/examples/recording --output-on-failure
build/examples/recording/gentest_recording --records=artifacts --junit=results.xml
```

## What the cases record

[cases.hpp](cases.hpp) records from fixtures and from the test body:

- The global fixture `Environment` records the device as a run property.
- The local fixture `Input` records the sample count and the raw input bytes
  in setup, and a `teardown_complete` property in teardown, for each case.
- The `sum` test records the sum it observed.

Serialization happens in fixture setup, outside the timed loops of the
benchmark and jitter cases.

`--records=artifacts` writes the records to a fresh `run-…` directory under
`artifacts`. Payload files get generated names; find each one through
`index.json`, never by the record name you chose. `results.xml` carries the
properties and a link to that `index.json`.
[expected_tests.txt](expected_tests.txt) lists the three case names.

## Optional JSON and CBOR snapshots

The default build uses C++20, scalar properties and raw bytes. To also record
a `Snapshot` object as JSON or CBOR, install the optional serializer and
configure with `-DRECORDING_WITH_GLAZE=ON`, `-DRECORDING_WITH_CBOR=ON`, or both:

- Glaze 8.3.0 writes JSON (`gentest/record_glaze.h`) and raises the consumer
  standard to C++23.
- cbor_tags 0.24.0 writes CBOR (`gentest/record_cbor.h`).

Both serialize the same `Snapshot` aggregate, and the application supplies its
schema identifier. Both records may use the name `snapshot`, because records
append independently.

When you enable Glaze, build this example with Clang. On the tested GCC 16
installation, GCC's implicit intrinsic headers conflict with gentest's
Clang-based codegen scan. The `runtime-recording` CI workflow builds this
example with both serializers through
`python3 scripts/validation_suites.py recording --work <dir> --serializers <Glaze and cbor_tags prefix>`.

For record scopes, export formats and dependency setup, see
[Runtime properties and structured records](../../docs/recording.md).
