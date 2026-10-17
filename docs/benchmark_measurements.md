# Benchmark execution and application timings

Include `<gentest/benchmark.h>` or `import gentest;`. These APIs require C++20,
the Gentest runtime and its existing fmt dependency; they introduce no serializer,
GPU API, event loop or tracing dependency.

## Choose execution at the case declaration

```cpp
[[using gentest: bench("encode")]]
void encode(); // Existing calibrated, runner-timed behavior.

[[using gentest: bench("encode/no-warmup"), bench_warmup(0)]]
void encode_without_warmup(); // Still calibrates and measures repeated epochs.

[[using gentest: bench("startup"), bench_once]]
void startup(); // Exactly one setup, call and teardown; no calibration or warmup.

[[using gentest: bench("transfer"), bench_once, bench_time("supplied")]]
void transfer(); // One call; the application publishes completed-work timing.
```

Source declarations take precedence over CLI defaults. For example,
`--bench-warmup=3 --bench-epochs=20` does not repeat a `bench_once` case, and
`bench_warmup(0)` disables warmup regardless of that CLI default. Other selected
cases keep their ordinary execution policy. `bench_once` cannot declare positive
warmup, and supplied timing requires `bench_once`. These options are benchmark-only;
jitter keeps its existing sampling policy. Invalid manual descriptors are rejected
before fixture setup, just as invalid declarations fail code generation.

`GENTEST_BENCHMARK_API_VERSION` is `1`. Updated CMake codegen helpers require this
generator capability explicitly, so an older generator cannot silently ignore the
new annotations. Rebuild registration sources and runtime together.

## Publish end-to-end elapsed time

```cpp
// Application-specific types/functions below stand for your existing workload.
void transfer(TransferFixture& sample) {
    sample.result = run_workload_to_completion(sample.options);
    verify_completed_work(sample.result);

    gentest::setBenchmarkDuration(
        std::chrono::nanoseconds{sample.result.elapsed_ns},
        sample.result.completed_items);
}
```

The timer must cover the work that the benchmark claims to measure: for an
end-to-end result, include completion and required result gathering, not merely
submission. Gentest never waits for application operations on your behalf.

Publish exactly once, from the owning thread, in the measured call **or its local
fixture teardown**. Teardown publication accommodates timing that is only available
after the measured call, without requiring asynchronous benchmark machinery. It
does not automatically expand the supplied interval: the application selects its
actual start/end boundaries.

Both elapsed time and completed items must be positive; elapsed time must be finite
and representable as nanoseconds. The duration-only overload uses the declaration's
`items_per_call` (default 1). An explicit runtime count **replaces**, rather than
multiplies, that static count: `2s, 1000` means 500 items/s even with
`items_per_call(16)`. Missing or duplicate publication fails the occurrence;
teardown still runs after a measured-call failure.

Reports retain measured-call wall time independently of supplied primary time.
Calibration and warmup are separate metadata; neither is silently included in the
supplied primary interval. A successful publication does not override any assertion,
exception or teardown failure.

## Add independent timing distributions in teardown

```cpp
void TransferFixture::tearDown() {
    gentest::recordTimingSamples("packet_latency", result.packet_latencies);

    // Raw unsigned device timestamps need an explicit unit, not a signed narrowing cast.
    gentest::recordTimingSamples("gpu_compute", result.gpu_elapsed_ns,
                                std::chrono::nanoseconds{1});

    auto transfers = result.frames | std::views::transform(
        [](const auto& frame) { return frame.transfer_duration; });
    gentest::recordTimingSamples("transfer", transfers);

    if (auto summary = gentest::currentMeasurement()) {
        // Owning copy: safe to retain or serialize with your other fixture data.
        retain_summary_for_application_report(*summary);
    }
}
```

Chrono ranges infer their unit; numeric ranges require a positive finite chrono
unit. Input ranges, including single-pass and lazy views, are consumed synchronously
without modifying their elements or retaining references. Phase, ownership and name
checks happen before evaluating a lazy range. Each name is published once. Invalid
values, duplicate names, iteration exceptions and allocation failures fail the case
without publishing a partial series or removing earlier accepted summaries.

Zero-duration observations are valid. Negative, non-finite or unrepresentable
observations are not. An empty range means **unavailable**: count zero with absent
statistics, represented as null in JSON/CSV and `-` in human reports, not fake zero
latency. Summaries use linear percentile interpolation and population standard
deviation. P05, P95 and P99 use the same sorted observations: for `{1, 2, 3, 4, 5}`
nanoseconds, P99 is `4.96 ns`. A singleton's P99 equals its only observation.
Exact statistics may use temporary storage; raw observations are not
retained or exported automatically.

Series have independent cardinalities and are never divided by primary item count.
CPU latency, partial GPU observations, transfer and compute can overlap. They are
not assumed to partition end-to-end time; Gentest does not invent an “unattributed”
residual by subtracting them.

Percentiles describe the supplied population: named packet-latency samples yield
packet P99, while a calibrated primary benchmark's P99 describes epoch averages.
P99 is available in `DurationSummary::p99_ns` and named timing reports; default
regression-comparison metrics are unchanged.

`currentMeasurement()` is valid only in local benchmark/jitter teardown. It returns
nullopt until a primary result is available, otherwise an independent owning
`MeasurementSnapshot` containing primary metadata/statistics and the named series
published so far. It is provisional: later teardown can still fail. It contains
neither final case outcome nor teardown-inclusive case wall time.

Workers collect observations into application-owned storage. Only the owner publishes
them after joining/draining; adopted-worker or absent-context misuse retains the
runner's existing fail-fast contract. These APIs do not introduce cross-thread
publication or scheduling.

## Reports and optional records

All four measured report formats (table, Markdown, JSON and CSV) include named
summaries. Primary metadata identifies execution, time source and sampling semantics;
the comparer lists incompatible timings under **Not Comparable**, including changed
jitter batch widths. Independent calibration can select different widths between
runs of the same binary. Such rows produce no regression or improvement delta;
compatible rows are still compared. The comparator retains the summary and exits
with an input error if no numeric comparisons remain. Malformed reports also remain
errors. In-report baseline deltas likewise require compatible execution, timing and
sampling policies; an incompatible baseline is unavailable, not a zero delta.
Named timing tables preserve their own series identity and raw nanosecond units.

For raw samples, correlations, device metadata or a timeline, use existing
[`record_json`, `record_cbor` or `record_data`](recording.md) explicitly in teardown.
The optional JSON adapter requires Glaze/C++23; the optional CBOR adapter requires
cbor_tags/C++20. A standard trace JSON file can be attached using `record_data`;
Gentest does not own its clocks, interval relationships or viewer. No new artifact
format or trace SDK is required.
