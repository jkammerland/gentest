# Runtime properties and structured records

Include `gentest/record.h` (also included by `test.h` and `runner.h`), or
`import gentest;`. The core API requires C++20 and no serialization library.

```cpp
gentest::record_property("seed", 42u);
gentest::record_property("device", "simulator");
std::array bytes{std::byte{0x01}, std::byte{0xff}};
gentest::record_data("capture", bytes, "application/octet-stream",
                     {.schema = "example.capture/v1"});
```

Properties accept null, bool, signed/unsigned integers up to 64 bits, finite
floating-point values up to double, and strings. They own their values; replacing
a key affects only its selected scope. NaN/infinity and empty keys fail the case.
Records copy their bytes immediately and append in call order. Duplicate names
are allowed. Names and content types must be nonempty; schema is an optional
identifier. The runner does not validate encoded data or interpret schemas.

## Scope and lifetime

`RecordScope::Current` is the default:

| Call location | Destination |
| --- | --- |
| Test body, local fixture allocation/setup/teardown | Current case occurrence |
| Suite fixture allocation/setup/teardown | Fixture's declaring suite |
| Global fixture allocation/setup/teardown | Run |

Use `RecordScope::Case`, `Suite`, or `Run` explicitly to select another available
scope. A test's suite destination is its resolved suite; shared fixture hooks use
the fixture's declaring suite. Shared hooks have no case destination, so explicitly
requesting `Case` there fails the fixture.

Async suspension preserves the destination, including teardown during cancellation.
Earlier data survives failures, skips, expected failures, exceptions, and fixture
failures. Fail-fast canceled async occurrences are marked `canceled` in the bundle.
Every repeated case gets its own occurrence ID; run and suite data live for the
whole invocation and include final teardown updates. A subsequent runner invocation
starts fresh. Exporters resolve properties in order: run, ancestor suites from
outermost to innermost, then case. Later scopes override the same key.

Recording requires the owning runner context. Calls before/after the runner or
from adopted worker threads trigger the existing context-misuse diagnostic.
Collect worker data separately, then record it on the owner. Benchmark and jitter
call phases reject recording; use fixture setup/teardown so serialization and
allocation stay outside timing loops. Adapter preflight must happen before encoding.

Invalid recording arguments report a normal nonfatal failure and retain earlier
valid data. Measured-call failures follow the runner's existing measured abort
behavior. Export failures make the runner return failure.

## Exports

```sh
./my_tests --records=artifacts --junit=results.xml
./my_tests --allure-dir=allure-results
```

`--records=DIR` creates a fresh `DIR/run-…/` directory. `index.json` has
`schemaVersion: 1`, run data, named suite data, case occurrences with declaration
metadata/outcome, and infrastructure errors known when the bundle was exported.
Each data object contains typed `properties` and a `records` array. Record entries
contain `sequence`, `name`, `contentType`, `schema`, and a relative `path`.
Generated numeric filenames prevent names from becoming paths. JSON and CBOR use
`.json` and `.cbor`; other bytes use `.bin`. Payloads are written before the index
is published by rename. A failed export may leave an incomplete directory without
a published index. Concurrent invocations use distinct child directories.

JUnit adds effective properties as `gentest.property.<key>`, preserves declared
requirements, and links `gentest.records` to the bundle index. With structured
records and only `--junit=results.xml`, the runner creates
`results.xml.records/run-…/` automatically. Scalars alone need no sidecar.

Allure exports effective scalar parameters, typed payload attachments, and a
per-case JSON scope index. Run/suite payloads are written once and referenced by
applicable cases. Allure retains its existing build-time Boost.JSON requirement.
Recording itself works without any exporter. Listing does not execute hooks or
create recording files.
