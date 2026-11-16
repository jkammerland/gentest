# Runtime properties and structured records

Record facts about a run, such as a seed or a device name, and attach data,
such as a capture or a snapshot, to the case, suite or run that produced it.
The runner exports both to a record directory, JUnit and Allure.

```cpp
#include "gentest/test.h"

#include <array>
#include <cstddef>

[[gentest::test("device/capture")]]
inline void capture() {
    gentest::record_property("seed", 42u);
    gentest::record_property("device", "simulator");

    const std::array bytes{std::byte{0x01}, std::byte{0xff}};
    gentest::record_data("capture", bytes, "application/octet-stream", {.schema = "example.capture/v1"});
}
```

```bash
./my_tests --records=artifacts --junit=results.xml
./my_tests --allure-dir=allure-results
```

The API is in `gentest/record.h`, which `gentest/test.h` and `gentest/runner.h`
include, and in `import gentest;`. It needs C++20 and no serialization library.
The [recording example](../examples/recording/) shares input preparation between
a test, a benchmark and a jitter case.

## Properties and records

`gentest::record_property(key, value)` sets a property. Values can be null,
`bool`, signed and unsigned integers up to 64 bits, finite floating-point values
up to `double`, and strings. A property owns its value. Setting a key again
replaces the value in the selected scope only. NaN, infinity and empty keys fail
the case.

`gentest::record_data(name, bytes, content_type, options)` appends a record.
It copies the bytes immediately, and records keep their call order. Duplicate
names are allowed. The name and the content type must not be empty;
`options.schema` is an optional identifier. The runner doesn't validate the
encoded data or interpret schemas.

An invalid argument reports a normal, non-fatal failure that starts with
`recording:`, and earlier valid data is kept.

## Scopes

Both functions take a `gentest::RecordScope`. For `record_property()` it is the
third argument, and for `record_data()` it is `options.scope`. The default,
`RecordScope::Current`, depends on where the call runs:

| Call location | Destination |
| --- | --- |
| Test body; local fixture allocation, setup and teardown | The current case [occurrence](command_line.md) |
| Suite fixture allocation, setup and teardown | The fixture's declaring suite |
| Global fixture allocation, setup and teardown | The run |

`RecordScope::Case`, `Suite` or `Run` selects another available scope. A case's
suite destination is its own suite; a [shared fixture](fixtures.md) hook's is the
suite that declares the fixture. Shared fixture hooks have no case, so asking
for `Case` there fails the fixture, and the cases that need it end blocked.

Exporters combine the scopes into one set of effective properties per case:
first the run, then the case's suites from outermost to innermost, then the case
itself. A later scope overrides the same key.

## Lifetime

- An async case keeps its destination across suspensions, including teardown
  during cancellation.
- Data recorded earlier survives failures, skips, expected failures, exceptions
  and fixture failures.
- An async case that `--fail-fast` cancels has the outcome `canceled` in the
  record index.
- Each occurrence, including each repeat, gets its own case `id`. Run and suite
  data cover the whole runner invocation and include updates made during final
  teardown. Another runner invocation starts fresh.
- Recording works without an exporter. Without one, each case's data is
  released when the case finishes.

## Where you can record

Record from the case itself or from its fixtures' hooks. A call from an
[adopted](logging_and_threads.md) worker thread, or from outside a runner
invocation, aborts the process with `gentest: fatal: recording called ...`.
Collect worker data separately, and record it from the test.

The timed call of a bench or jitter case rejects recording with
`recording: recording is forbidden in a timed bench/jitter call; use fixture setup/teardown`.
Like any failure in a timed call, it ends the measurement. Record from fixture
setup or teardown instead, so serialization and allocation stay outside the
timing loop.

For timing statistics, use `recordTimingSamples()` and `currentMeasurement()`
instead (see [Benchmarks](benchmarks.md)); recording a payload adds no
benchmark statistics.

## Exports

`--records=DIR` writes a fresh `DIR/run-…/` directory for each invocation. Its
`index.json` lists everything recorded:

| Key | Contents |
| --- | --- |
| `schemaVersion` | `1` |
| `run` | The run's data. |
| `suites` | Suite data, as objects with `name` and `data`. |
| `cases` | One object per case occurrence: `id`, `name`, `suite`, `kind` (`test`, `bench` or `jitter`), `file`, `line`, `owner`, `requirements`, `tags`, `outcome` and `data`. |
| `errors` | Infrastructure errors known when the index was written. |

Each `data` object has typed `properties` and a `records` array. A record entry
has `sequence`, `name`, `contentType`, `schema` and `path`, which is relative to
the run directory. `outcome` is one of `pass`, `fail`, `skip`, `blocked`,
`xfail`, `xpass` or `canceled`.

Payload files have generated names; find each file through its `path`. The
extension is `.json` for `application/json`, `.cbor` for `application/cbor` and
`.bin` for anything else. The runner publishes `index.json` last, so a failed
export can leave a directory without an index. Concurrent invocations write
separate directories.

`--junit=FILE` adds each case's effective properties as
`gentest.property.<key>` properties, keeps its declared requirements as
`requirement` properties, and links the index in a `gentest.records` property.
If the run recorded `record_data()` payloads but `--records` isn't given, the
runner writes the directory next to the report, as `FILE.records/run-…/`.
Properties alone need no directory.

`--allure-dir=DIR` adds the effective properties as `gentest.property.<key>`
parameters, each record as an attachment with its content type, and a per-case
JSON index of the scopes that apply (the `runtime record index` attachment). Run
and suite payloads are written once and referenced by every case they apply to.
See [Reports](reports.md) for the rest of the JUnit and Allure output.

Listing options, such as `--list-json`, don't run hooks or create record files.
A failed export makes the runner return failure.

## Serialize objects

Two optional headers serialize an object and record it in one call:

```cpp
#include "gentest/record_cbor.h"  // link cbor::tags (C++20)
#include "gentest/record_glaze.h" // link glaze::glaze (C++23)
#include "gentest/test.h"

#include <string>
#include <vector>

namespace snapshots {

struct Snapshot {
    std::string      device;
    std::vector<int> samples;
};

[[gentest::test("record_snapshot")]]
inline void record_snapshot() {
    const Snapshot value{"simulator", {1, 2, 3}};
    gentest::record_json("snapshot", value, {.schema = "snapshot/v1"});
    gentest::record_cbor("snapshot", value, {.schema = "snapshot/v1"});
}

} // namespace snapshots
```

The adapters are tested with [Glaze 8.3.0](https://github.com/stephenberry/glaze/tree/v8.3.0)
and [cbor_tags 0.24.0](https://github.com/jkammerland/cbor_tags/tree/v0.24.0).
`import gentest;` doesn't provide them, and the gentest package doesn't depend on
either library. Each uses the library's default options plus your customizations
(`glz::meta` for Glaze; aggregate, tag and custom codec rules for cbor_tags).
Unsupported types produce compiler errors.

An encoding error or a caught serializer exception is a non-fatal failure that
appends nothing. Glaze works in consumers built without exceptions; the default
cbor_tags encoder needs exceptions. The adapters follow the same scope and
timed-call rules as `record_data()`. For other formats or options, serialize the
object yourself and pass the bytes to `record_data()`.

The [recording example](../examples/recording/) turns the adapters on with
`-DRECORDING_WITH_GLAZE=ON` and `-DRECORDING_WITH_CBOR=ON`; its README has notes
on compilers.

## Test the adapters in this repository

To build and run gentest's own adapter tests (`GENTEST_ENABLE_RECORDING_ADAPTER_TESTS`),
see [Contributing to gentest](../CONTRIBUTING.md).
