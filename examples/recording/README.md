# Runtime records

Three cases share input preparation: a correctness test, benchmark, and jitter.
A global fixture records the device; local fixtures record input and teardown
properties for each case. Serialization happens in setup, outside timing loops.

```sh
cmake -S examples/recording -B build/examples/recording -G Ninja \
  -DCMAKE_PREFIX_PATH=/path/to/gentest/install
cmake --build build/examples/recording
ctest --test-dir build/examples/recording --output-on-failure
./build/examples/recording/gentest_recording --records=artifacts --junit=results.xml
```

The default C++20 build uses scalar properties and raw bytes. To add JSON or CBOR,
install the optional dependencies and configure with `-DRECORDING_WITH_GLAZE=ON`
and/or `-DRECORDING_WITH_CBOR=ON`. Glaze raises the consumer standard to C++23.
Both can serialize the same `Snapshot` aggregate; its schema identifier is supplied
by the application. The record names may match because records append independently.

Tested serializer versions: Glaze 8.3.0 and cbor_tags 0.24.0. See
[recording and dependency setup](../../docs/recording.md). Use Clang for this
example when enabling Glaze: GCC implicit intrinsic headers currently conflict
with Gentest's Clang-based scanner on the tested GCC 16 installation.

`expected_tests.txt` lists all three resolved names. Exported payload files have
generated names; locate each one through `index.json`, never by the user record name.
