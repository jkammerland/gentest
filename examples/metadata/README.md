# Declared test metadata

This example attaches a requirement, an owner and a tag to a case, then reads
them back from the JSON inventory and the JUnit report. A case declares its
metadata in the same attribute as its name:

```cpp
[[using gentest: test("bounded_value"), parameters(value, -1, 11),
  req("LIMIT-001"), owner("examples"), fast]]
inline void boundedValue(int value);
```

Both cases that `parameters(value, -1, 11)` generates inherit the requirement,
the owner and the `fast` tag. The third case, `metadata/plain`, declares no
metadata, so its owner, requirements and tags are empty.
[expected_tests.txt](expected_tests.txt) lists all three names.

Build it, then export the inventory and the execution results:

```bash
cmake -S examples/metadata -B build/examples/metadata -G Ninja -DCMAKE_PREFIX_PATH=/path/to/gentest/install
cmake --build build/examples/metadata
ctest --test-dir build/examples/metadata --output-on-failure
build/examples/metadata/gentest_metadata --list
build/examples/metadata/gentest_metadata --list-json > inventory.json
build/examples/metadata/gentest_metadata --junit=results.xml
```

## What each output contains

- `--list` prints each case with its metadata and source location, for
  example
  `metadata/bounded_value(-1) [gentest:tags=owner=examples,fast;requires=LIMIT-001;owner=examples] (cases.hpp:11)`.
  The owner also appears as an `owner=examples` tag.
- `inventory.json` is a JSON array with each case's name, file and line, kind,
  tags, requirements, owner and other registry fields. It describes the
  compiled cases, not their pass or fail outcomes.
- `results.xml` records the outcomes and exports each `req(...)` as a JUnit
  `requirement` property. JUnit has no owner or tag properties: to connect
  owners and tags to outcomes, join the inventory's `name` with the JUnit
  testcase `name`.

For evidence produced while a case runs, use `gentest::record_property()` and
`gentest::record_data()`.
[Runtime properties and structured records](../../docs/recording.md) explains
their scopes, payloads and exports, and the
[recording example](../recording/) uses them.

## Learn more

- [Writing tests](../../docs/writing_tests.md): the attributes, including the
  fixed set of tags.
- [Test inventory](../../docs/test_inventory.md): every `--list-json` field.
- [Requirements traceability](../../docs/traceability_standards.md):
  requirements with `req(...)`.
- [Reports](../../docs/reports.md): JUnit and the other report formats.
