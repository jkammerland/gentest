# Recording declared test metadata

Gentest supports metadata on case declarations:

```cpp
[[using gentest: test("bounded_value"), parameters(value, -1, 11),
  req("LIMIT-001"), owner("examples"), fast]]
inline void boundedValue(int value);
```

Both expanded cases inherit the requirement, owner, and custom `fast` tag.
The third case has no metadata, demonstrating the empty owner/requirements/tag
fields. [expected_tests.txt](expected_tests.txt) records all three names.

Build and export the inventory and execution results:

```sh
cmake -S examples/metadata -B build/examples/metadata -G Ninja \
  -DCMAKE_PREFIX_PATH=/path/to/gentest/install
cmake --build build/examples/metadata
ctest --test-dir build/examples/metadata --output-on-failure

./build/examples/metadata/gentest_metadata --list
./build/examples/metadata/gentest_metadata --list-json > inventory.json
./build/examples/metadata/gentest_metadata --junit=results.xml
```

`inventory.json` is a JSON array with each case's name, file/line, kind, tags,
requirements, owner, and other registry fields. It describes compiled cases;
it is not a record of pass/fail outcomes. `results.xml` records execution outcomes
and exports each `req(...)` as a JUnit `requirement` property. Join the inventory's
`name` with the JUnit testcase `name` to associate owners and tags with outcomes;
JUnit does not currently export those as dedicated properties.

Runtime evidence is available through `gentest::record_property()` and
`gentest::record_data()`. See [runtime recording](../../docs/recording.md) for
case/suite/run scope, structured payloads, and exporter behavior. This example
focuses on declaration metadata and inventory.

The package smoke test parses JSON and JUnit to verify inherited metadata on both
expanded cases and empty metadata on the plain case. See the
[test inventory guide](../../docs/test_inventory.md),
[traceability guide](../../docs/traceability_standards.md), and
[examples index](../README.md).
