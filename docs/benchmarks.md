# Benchmarks and jitter

Use benchmarks to measure how long code takes, and jitter cases to measure how
much that time varies from call to call. Both are **measured cases**: functions
marked with an attribute and run by the same test executable as your tests.

## Write a benchmark

Declare measured cases where you declare tests (see
[Writing tests](writing_tests.md)); they need no extra build setup.

```cpp
#pragma once

#include "gentest/bench_util.h"
#include "gentest/runner.h"

#include <cmath>
#include <string>

namespace strings {

// Setup and the result check each run once, outside the timed calls.
struct ConcatInputs : gentest::FixtureSetup, gentest::FixtureTearDown {
    std::string left, right, result;

    void setUp() override {
        left  = "hello";
        right = " world";
    }

    void tearDown() override { gentest::asserts::EXPECT_EQ(result, "hello world"); }
};

// One timed call is one complete concatenation.
[[gentest::bench("concat")]]
inline void concat(ConcatInputs &inputs) {
    inputs.result = inputs.left + inputs.right;
    gentest::doNotOptimizeAway(inputs.result.data());
    gentest::clobberMemory();
}

// How much does the time of a single std::sin call vary?
[[gentest::jitter("sin")]]
inline void sine() {
    volatile double x = 1.2345;
    gentest::doNotOptimizeAway(std::sin(x));
}

} // namespace strings
```

- `bench("concat")` declares a benchmark and `jitter("sin")` a jitter case.
  Unlike `test`, both always take a name. The namespace is the suite, so the
  full names are `strings/concat` and `strings/sin`.
- `ConcatInputs` is a local fixture: gentest creates it and calls `setUp()` once
  before the timed calls and `tearDown()` once after them (see
  [Fixtures](fixtures.md)). Prepare inputs and check results there, outside the
  timing.
- `doNotOptimizeAway()` and `clobberMemory()` stop the compiler from deleting
  the work; see
  [Keep the work from being optimized away](#keep-the-work-from-being-optimized-away).
- Templates and value parameters work as they do on tests, and each generated
  case is measured separately (see [Parameters](parameters.md)).

For meaningful numbers, build the test target with optimization, for example
with `-DCMAKE_BUILD_TYPE=Release`. Then run it:

```bash
./my_tests --list-benches
./my_tests --kind=bench
./my_tests --kind=jitter
./my_tests --run=strings/concat
```

`--list-benches` prints the names of all benchmark and jitter cases. `--kind`,
`--run`, `--filter` and tags select measured cases the same way they select
tests (see [Selecting tests](test_selection.md)). A plain `./my_tests` measures
too; pass `--kind=test` to run only the tests.

[examples/measured](../examples/measured) is a complete project that tests,
benchmarks and jitter-measures one function for several template arguments.

### Measured cases under CTest

[`gentest_discover_tests()`](discover_tests.md) registers every measured case as
a CTest test too. To keep CTest fast, pass short settings with `EXTRA_ARGS`, and
run the executable directly for real measurements:

```cmake
gentest_discover_tests(my_tests EXTRA_ARGS
    --bench-epochs=3 --bench-warmup=1
    --bench-min-epoch-time-s=0.0001 --bench-min-total-time-s=0
    --bench-max-total-time-s=0.02)
```

`EXTRA_ARGS` goes to every CTest test of the executable. The `--bench-*`
options don't change how tests run.

## How measurement works

Each measured case runs in three phases:

1. **Setup**, once: gentest creates the case's local fixtures and calls their
   `setUp()`.
2. **Call**, many times: gentest calls your function in timed loops. All
   reported timing comes from this phase.
3. **Teardown**, once: gentest calls the local fixtures' `tearDown()`.

One local fixture object serves all the calls of a case; suite and global
fixtures keep their usual lifetimes.

A benchmark times **epochs**: loops of calls that are timed as a whole. An
epoch's time divided by its number of calls is one sample.

1. **Calibration** starts with one call per epoch and doubles the count until
   one epoch takes at least `--bench-min-epoch-time-s` (default 0.01 s).
2. **Warmup** runs `--bench-warmup` epochs (default 1) of that size and
   discards them.
3. **Measurement** runs epochs until at least `--bench-epochs` of them (default
   12) have run and their total time has reached `--bench-min-total-time-s`
   (default 0). Once that minimum total time is reached, measurement also stops
   when it has taken longer than `--bench-max-total-time-s` (default 1 s; `0`
   removes the limit), even before `--bench-epochs` epochs.

A benchmark's statistics describe epoch averages, not single calls, and a slow
case can end with fewer samples than `--bench-epochs`. Jitter cases use the same
options but record each call, or each small batch of calls, as its own sample
(see [Jitter sampling](#jitter-sampling)).

gentest reads `std::chrono::steady_clock` around the timed loops and doesn't
subtract the cost of the loop, the generated wrapper or the clock reads.

The tests of an invocation run first, then the selected benchmarks, then the
selected jitter cases, each in inventory order (the order `--list-benches`
prints). Each measured case runs once per invocation: `--repeat` and `--shuffle`
apply only to tests.

For example, for more samples and at least 0.2 s of measured time per case:

```bash
./my_tests --kind=bench --bench-epochs=20 --bench-warmup=5 --bench-min-total-time-s=0.2 --bench-max-total-time-s=1
```

[Command-line options](command_line.md) lists every option for measured runs,
with its accepted values.

## Read the results

After the cases have run, the runner prints the measured tables. The
`Benchmarks` table has one row per benchmark:

| Column | Meaning |
|---|---|
| `Samples` | Measured epochs. A `bench_once` case has one. |
| `Iters/epoch` | Calls per epoch, chosen by calibration. |
| `Items/call` | The case's [`items_per_call`](#count-items-per-call), 1 by default. |
| `Median/item`, `Mean/item`, `P05/item`, `P95/item`, `Worst/item` | Statistics of the epoch samples, divided by `Items/call`. |
| `Total` | Time spent in calls, including calibration and warmup. Setup and teardown aren't included. |
| `Baseline Δ%` | Difference of `Median/item` from the suite's [baseline](#compare-cases-with-a-baseline) case, or `-`. |
| `Execution`, `Time source`, `Sampling` | How the case was measured: `calibrated` or `once`; `runner` or `supplied`; `epoch-average` or `single-call`. |

The `Bench debug` table shows total calls and items, the time spent measuring,
in warmup and in calibration, the settings used, and the throughput
(`Calls/sec`, `Items/sec`). A `Benchmark measurements` table follows when a case
published [named timing series](#record-named-timing-series). Jitter cases get
`Jitter summary` and `Jitter debug` tables and a histogram per case (see
[Jitter sampling](#jitter-sampling)).

Each time cell picks its own unit (`ns`, `us`, `ms` or `s`). `--time-unit=ns`
prints every time in nanoseconds and moves the unit into the header, as in
`Median (ns/item)`. `--report-format=markdown` prints the same tables as
Markdown. For tools, save the results [as JSON or CSV](#save-reports-as-json-or-csv).

## Keep the work from being optimized away

An optimizing compiler removes work whose result is never used, and the
benchmark then times nothing. `gentest/bench_util.h` has two compiler barriers:

- `gentest::doNotOptimizeAway(value)` makes the compiler assume that `value` is
  used, so the code that computes it must run. It keeps the whole object you
  pass, but not memory that the object points to. To keep separately owned
  storage, such as the characters of a `std::string`, pass a pointer to it
  (`result.data()`) and call `clobberMemory()` after it.
- `gentest::clobberMemory()` makes the compiler assume that any memory may be
  read or written at that point, so it can't drop earlier stores or reuse values
  it loaded before.

Neither stops the compiler from folding a computation on inputs it can see at
compile time, so read inputs at run time, for example from a fixture or a
`volatile`. They constrain the compiler, not the CPU, and don't synchronize
threads.

With MSVC (but not clang-cl), `doNotOptimizeAway()` calls an opaque function in
the gentest runtime instead of using inline assembly; that call and the store of
the value count toward the measured time.

With named modules, `import gentest.bench_util;` provides both functions;
`import gentest;` doesn't (see [Named modules](modules.md)).

## Count items per call

By default the time columns are per call. When one call does several units of
work, declare how many with `items_per_call(N)` (alias `ops_per_call(N)`). The
time columns are then divided by `N`, and `Items/call` shows `N`. This example
uses `ConcatInputs` from the first example:

```cpp
// Four concatenations per call: the time columns are divided by four.
[[using gentest: bench("concat_x4"), items_per_call(4)]]
inline void concat_x4(ConcatInputs &inputs) {
    for (int i = 0; i < 4; ++i) {
        inputs.result = inputs.left + inputs.right;
        gentest::doNotOptimizeAway(inputs.result.data());
        gentest::clobberMemory();
    }
}
```

- Count completed work, not inputs: concatenating three strings into one result
  is one item.
- Write `N` as a plain positive decimal integer, without a prefix, suffix, digit
  separator or leading zero: `items_per_call(1024)` is valid;
  `items_per_call(0x400)` and `items_per_call(1'024)` are errors.
- `Iters/epoch` and `Calls/sample` count calls, not items.
  [Named timing series](#record-named-timing-series) are never divided by `N`.

## Compare cases with a baseline

Mark one benchmark or jitter case in a suite with `baseline` to compare the
suite's other cases with it:

```cpp
// The other benchmarks in namespace strings are compared with this one.
[[using gentest: bench("concat"), baseline]]
inline void concat(ConcatInputs &inputs) {
    inputs.result = inputs.left + inputs.right;
    gentest::doNotOptimizeAway(inputs.result.data());
    gentest::clobberMemory();
}
```

`Baseline Δ%` is the difference between a case's `Median/item` and the
baseline's, as a percentage of the baseline; the baseline itself shows `+0.00%`.
Jitter summaries add `Baseline SD Δ%` for the standard deviation per item. In
JSON and CSV the values are `baseline_delta_pct` and
`baseline_stddev_delta_pct`.

- `baseline` requires `bench` or `jitter` on the same declaration.
- A case is compared only with a baseline measured the same way (see
  [Measurements that are not comparable](#measurements-that-are-not-comparable));
  otherwise its delta is `-` in tables and `null` in JSON and CSV.
- The baseline must run in the same invocation; if your selection leaves it out,
  there are no deltas.
- If two baseline cases of a suite were measured the same way, the first one in
  run order is used.

## Jitter sampling

A jitter case reads the clock before and after each call, so each sample is the
time of one call. If calibration shows that one call takes less than ten times
the cost of the two clock reads, gentest switches to **batch sampling**: each
sample is the average of a batch of calls timed together, with up to 64 samples
per epoch. Batch averages hide spikes in single calls, and the report says so:

- `Sampling` is `per-call` or `batch-average`, and `Calls/sample` is the batch
  size.
- Each batch-averaged case adds a warning above the table, for example:

  ```text
  WARNING: strings/sin: batch sampling active (8192 calls/sample). Histogram, percentiles, standard deviation and maximum describe batch averages; individual-call latency spikes may be hidden.
  ```

- The maximum column is `Max` when every row samples single calls,
  `Max batch avg` when every row samples batch averages, and `Max sample` when
  the table has both.
- A batch of one call counts as per-call sampling and gets no warning.

`Jitter debug` shows `Mode`, the timing loop that ran (`batch` or `per-call`),
and the estimated timing overhead per call (`Overhead/call`, and `Overhead %` of
the median). The overhead is reported, not subtracted.

Each jitter case also gets a histogram with `--jitter-bins=N` equal-width bins
(default 10, at most 10000) from the smallest to the largest sample, per item.
Each row shows the count, percentage and cumulative percentage. If two bins
would print the same range at the displayed precision, the table merges them and
prints a note on stderr.

JSON, CSV and Allure output describe the sampling with these fields:

| Field | Meaning |
|---|---|
| `mode` | `batch` or `per-call`: the timing loop that ran. |
| `sample_kind` | `batch-average` when each sample averages several calls, otherwise `per-call`. A batch of one call has `mode` `batch` and `sample_kind` `per-call`. |
| `calls_per_sample` | The batch size. |
| `sampling_warning` | The warning text, or `null` when samples don't average several calls. Allure metrics leave the field out in that case. In JSON and CSV the warning appears only here. |

## Choose how a benchmark executes

Three attributes change how one benchmark is calibrated and timed. They need
`bench`, and can't be combined with `test` or `jitter`:

```cpp
[[gentest::bench("encode")]]
void encode(); // calibrated and timed by the runner (the default)

[[using gentest: bench("encode/no_warmup"), bench_warmup(0)]]
void encode_without_warmup(); // calibrated, without warmup epochs

[[using gentest: bench("startup"), bench_once]]
void startup(); // exactly one setup, one call and one teardown

[[using gentest: bench("transfer"), bench_once, bench_time("supplied")]]
void transfer(); // one call; your code reports the elapsed time
```

These are declarations; define the functions in a `.cpp` file, or in the header
with `inline` (see [Writing tests](writing_tests.md)).

- `bench_warmup(N)` sets this benchmark's warmup epochs (a plain decimal
  integer, 0 or more).
- `bench_once` calls the function once, without calibration or warmup, and
  can't be combined with a positive `bench_warmup`. The report shows `once` and
  `single-call`.
- `bench_time("supplied")` means that your code reports the time; it requires
  `bench_once`. `bench_time("runner")` is the default.

The declaration wins over the command line: `--bench-epochs=20` doesn't repeat a
`bench_once` case, and `bench_warmup(0)` ignores `--bench-warmup`.

## Supply your own timing

Some work isn't a call that the runner can time: a transfer that finishes on a
device, a request that another thread completes, a pipeline that times itself.
Declare such a benchmark with `bench_once` and `bench_time("supplied")`, and
report its time with `gentest::setBenchmarkDuration()` from
`gentest/benchmark.h` (also in `import gentest;`):

```cpp
#pragma once

#include "gentest/benchmark.h"
#include "gentest/runner.h"

#include <chrono>
#include <cstdint>

namespace transfers {

// Stand-ins for your application's code.
struct TransferResult {
    std::chrono::nanoseconds elapsed{}; // submit to completion, timed by your code
    std::uint64_t            completed_items = 0;
};
TransferResult run_transfer_to_completion();

struct TransferFixture {
    TransferResult result;
};

[[using gentest: bench("transfer"), bench_once, bench_time("supplied")]]
inline void transfer(TransferFixture &fixture) {
    fixture.result = run_transfer_to_completion();
    gentest::setBenchmarkDuration(fixture.result.elapsed, fixture.result.completed_items);
}

} // namespace transfers
```

- Call `setBenchmarkDuration()` exactly once, on the test's own thread, in the
  timed call or in a local fixture's `tearDown()`.
- gentest doesn't wait for your asynchronous work and takes the interval as you
  pass it, so for an end-to-end time include completion, not just submission.
- `setBenchmarkDuration(elapsed)` uses the case's `items_per_call` (1 by
  default). `setBenchmarkDuration(elapsed, items)` replaces that count instead
  of multiplying it: `setBenchmarkDuration(2s, 1000)` reports 2 ms per item,
  even with `items_per_call(16)`.
- The report keeps the runner's own time of the call (`Total`,
  `measured_call_wall_time_s`) next to the supplied time.
- A supplied time doesn't hide a failure: assertions, exceptions and a failing
  teardown fail the case anyway.

The case fails with one of these messages when the time is missing or invalid:

| Problem | Message |
|---|---|
| `setBenchmarkDuration()` wasn't called. | `benchmark duration was not supplied` |
| It was called a second time. | `benchmark duration was already supplied` |
| The time is zero, negative, not finite or too large, or the item count is zero. | `benchmark duration and completed items must be positive and representable` |
| The case isn't `bench_once` with `bench_time("supplied")`, or the call happens outside the timed call and teardown. | `supplied duration requires a once/supplied measured call or local teardown` |

## Record named timing series

A measured case can also publish series of observations that your code
collects, such as per-packet latencies or GPU kernel times. gentest summarizes
each series and reports it in a `Benchmark measurements` or
`Jitter measurements` table, next to the case's own timing:

```cpp
#pragma once

#include "gentest/benchmark.h"
#include "gentest/runner.h"

#include <chrono>
#include <cstdint>
#include <ranges>
#include <vector>

namespace pipeline {

struct Frame {
    std::chrono::microseconds transfer_duration{};
};

struct PipelineRun;
void run_pipeline(PipelineRun &run);                                          // your code
void save_for_application_report(const gentest::MeasurementSnapshot &snapshot); // your code

struct PipelineRun : gentest::FixtureTearDown {
    // Filled by your code during the call.
    std::vector<std::chrono::nanoseconds> packet_latencies;
    std::vector<std::uint64_t>            gpu_elapsed_ns; // raw device counters
    std::vector<Frame>                    frames;

    void tearDown() override {
        // A range of std::chrono durations carries its unit.
        gentest::recordTimingSamples("packet_latency", packet_latencies);

        // Plain numbers need the unit of one count.
        gentest::recordTimingSamples("gpu_compute", gpu_elapsed_ns, std::chrono::nanoseconds{1});

        // Any input range works, including lazy views.
        auto transfers = frames | std::views::transform([](const Frame &frame) { return frame.transfer_duration; });
        gentest::recordTimingSamples("transfer", transfers);

        // An owning copy of the primary result and the series published so far.
        if (const auto snapshot = gentest::currentMeasurement()) {
            save_for_application_report(*snapshot);
        }
    }
};

[[using gentest: bench("pipeline"), bench_once]]
inline void pipeline(PipelineRun &run) { run_pipeline(run); }

} // namespace pipeline
```

- Call `recordTimingSamples()` in the teardown of a measured case, on the
  test's own thread. Anywhere else the case fails with
  `timing samples require local benchmark/jitter teardown`. From a worker
  thread, even an [adopted](logging_and_threads.md) one, it aborts the process
  (as do `setBenchmarkDuration()` and `currentMeasurement()`), so collect
  worker observations yourself and publish them after the workers finish.
- Publish each non-empty name once per case.
- A range of `std::chrono::duration` values carries its unit. A range of plain
  numbers (integers or floating point, not `bool`) needs a positive, finite unit
  as the third argument.
- gentest reads the range once, right away, so single-pass ranges and lazy
  views work.
- Samples may be zero. A negative, non-finite or too large sample, or an
  exception while reading the range, fails the case and publishes nothing of
  that series; earlier series remain.
- An empty range publishes an unavailable series: count 0, shown as `-` in
  tables and `null` in JSON and CSV.

Each summary has the count, minimum, maximum, mean, median, population standard
deviation, P05, P95 and P99, in nanoseconds. Percentiles interpolate linearly
between the sorted samples: for samples of 1, 2, 3, 4 and 5 ns, P99 is 4.96 ns.
Series are independent of each other and of the primary timing: they aren't
divided by `items_per_call`, may overlap, and needn't add up to the primary
time.

gentest keeps only the summary. To export raw samples, device metadata or a
trace, record them in teardown with the [recording API](recording.md).

### Read the measurement in teardown

`gentest::currentMeasurement()`, used at the end of the example above, returns
what has been measured so far as a `gentest::MeasurementSnapshot`. It is an
owning copy that you can keep or serialize:

- `primary`: the case's own timing. Its `kind`, `execution`, `time_source` and
  `sampling` say how the case was measured. Its `statistics` hold a sample count
  and, when available, a `DurationSummary` (`min_ns`, `max_ns`, `mean_ns`,
  `median_ns`, `stddev_ns`, `p05_ns`, `p95_ns`, `p99_ns`). It also has the call
  and item counts and the seconds spent measuring, in warmup and in calibration.
  The summary tables don't show the primary P99; read it here.
- `timings`: the named series published so far, in order.

Call it only in the teardown of a measured case; elsewhere it fails the case.
It returns `std::nullopt` while there is no primary result, for example in a
supplied-time case before `setBenchmarkDuration()`. The snapshot is
provisional: it has neither the final outcome nor the time of the teardown.

Primary statistics are in nanoseconds per call, not per item:
`setBenchmarkDuration(2s, 1000)` gives a snapshot `median_ns` of `2e9` and a
report `median_ns_per_item` of `2e6`.

## Save reports as JSON or CSV

`--report-format=json` prints the measured results as one JSON document, and
`--report-format=csv` prints them as CSV. Both use fixed field names, such as
`median_ns_per_item`, `items_per_call` and `baseline_delta_pct`, and plain
nanoseconds or seconds instead of display units:

```bash
./my_tests --kind=bench --report-format=json > bench.json
./my_tests --kind=jitter --report-format=csv > jitter.csv
```

These formats need a selection of measured cases only (`--kind=bench`,
`--kind=jitter`, or a filter that matches no tests); otherwise the runner exits
with `1` before it runs anything:

```text
error: --report-format=json requires a measured-only selection; use --kind=bench, --kind=jitter, or a measured-only filter
```

The JSON document has three members:

- `report`: `bench`, `jitter`, or `measured` when the run has both kinds.
- `tables`: one object per table, with `report`, `id`, `title` and `rows`. Each
  row is an object of fields.
- `issues`: one object per measured case that failed, was skipped or was
  blocked, plus runner-level failures, with `name`, `file`, `line`, `message`
  and `infrastructure`. A skipped case's `message` starts with `SKIP` and a
  blocked case's with `BLOCKED:`. These cases have no table rows.

CSV has one line per field under the header `report,table,row,field,type,value`.
`type` is `string`, `number`, `bool` or `null`, and the issues appear in a table
named `issues`. The output starts like this:

```text
report,table,row,field,type,value
bench,bench.summary,0,benchmark,string,strings/concat
bench,bench.summary,0,suite,string,strings
bench,bench.summary,0,is_baseline,bool,false
bench,bench.summary,0,execution_mode,string,calibrated
```

### Fields

| Table `id` | One row per | Fields |
|---|---|---|
| `bench.summary` | benchmark | `benchmark`, `suite`, `is_baseline`, `execution_mode`, `time_source`, `sampling_kind`, `samples`, `iters_per_epoch`, `items_per_call`, `median_ns_per_item`, `mean_ns_per_item`, `p05_ns_per_item`, `p95_ns_per_item`, `worst_ns_per_item`, `wall_time_s`, `measured_call_wall_time_s`, `baseline_delta_pct` |
| `bench.debug` | benchmark | `benchmark`, `suite`, `epochs`, `iters_per_epoch`, `items_per_call`, `total_calls`, `total_items`, `measured_time_s`, `measured_call_wall_time_s`, `wall_time_s`, `warmup_time_s`, `calibration_iters`, `calibration_time_s`, `min_epoch_time_s`, `min_total_time_s`, `max_total_time_s`, `calls_per_sec`, `items_per_sec` |
| `jitter.summary` | jitter case | `benchmark`, `suite`, `is_baseline`, `execution_mode`, `time_source`, `sampling_kind`, `mode`, `sample_kind`, `calls_per_sample`, `sampling_warning`, `samples`, `items_per_call`, `median_ns_per_item`, `mean_ns_per_item`, `stddev_ns_per_item`, `p05_ns_per_item`, `p95_ns_per_item`, `min_ns_per_item`, `max_ns_per_item`, `wall_time_s`, `measured_call_wall_time_s`, `baseline_delta_pct`, `baseline_stddev_delta_pct` |
| `jitter.debug` | jitter case | `benchmark`, `suite`, `mode`, `batch_mode`, `sample_kind`, `calls_per_sample`, `samples`, `iters_per_epoch`, `items_per_call`, `overhead_mean_ns_per_call`, `overhead_sd_ns_per_call`, `overhead_ratio_pct`, `measured_time_s`, `measured_call_wall_time_s`, `warmup_time_s`, `min_total_time_s`, `max_total_time_s`, `wall_time_s` |
| `jitter.histogram` | histogram bin | `benchmark`, `suite`, `mode`, `sample_kind`, `calls_per_sample`, `bin`, `range_lo_ns_per_item`, `range_hi_ns_per_item`, `inclusive_hi`, `count`, `percent`, `cumulative_percent` |
| `bench.measurements`, `jitter.measurements` | named series | `benchmark`, `suite`, `series`, `count`, `execution_mode`, `time_source`, `sampling_kind`, `min_ns`, `max_ns`, `mean_ns`, `median_ns`, `stddev_ns`, `p05_ns`, `p95_ns`, `p99_ns` |

- `execution_mode` is `calibrated` or `once`, and `time_source` is `runner` or
  `supplied`.
- `sampling_kind` is `epoch-average` for a calibrated benchmark, `single-call`
  for `bench_once`, `per-call` or `batch-average` for a jitter case, and
  `observation` for a named series.
- `wall_time_s` covers all calls, including calibration and warmup.
  `measured_call_wall_time_s` is the runner's time for the measured calls only.
- Values that don't exist are `null`, such as `baseline_delta_pct` without a
  baseline.

### Keep stdout for the report

With `--report-format=json` or `csv` and a selection of measured cases, stdout
carries only the report, so you can redirect it to a file:

- Skip and blocked lines, failure messages, `Rerun:` lines, notes, GitHub
  annotations and the run summary (printed only when something failed) go to
  stderr.
- The default `gentest::log()` sink writes to stderr. Add
  `--log-output=failures` to print captured logs only for failed cases (see
  [Logging and threads](logging_and_threads.md)).
- Log sinks that your program installs, and anything your code prints itself,
  are not redirected, so keep them off stdout.

## Compare reports between runs

`scripts/compare_measured_reports.py` compares a saved JSON or CSV report (the
baseline report) with a later one, and fails when a case got slower by more
than a threshold. It needs only Python 3. `cmake --install` doesn't install it,
so run it from a gentest checkout or copy it into your project:

```bash
python3 scripts/compare_measured_reports.py \
  --baseline baseline.json \
  --current current.json \
  --fail-regression-pct 10 \
  --markdown-out measured-compare.md
```

The script:

- reads the summary rows and the named-series rows of both reports, and
  matches them by kind, case name and series name;
- compares `median_ns_per_item`, `p95_ns_per_item` and `stddev_ns_per_item` for
  summary rows, and `median_ns`, `p95_ns` and `stddev_ns` for named series
  (benchmark rows have no `stddev_ns_per_item` and list it under
  `Omitted Metrics`). `--metric <name>`, which you can repeat, compares only
  the given fields, for every row; a requested field that only one report has
  is an error;
- reports a regression when the current value is larger by more than
  `--fail-regression-pct` percent (default 10) and by more than
  `--min-regression-delta` (default 0) in absolute terms;
- lists cases that only the current report has under `New Benchmarks`, and
  cases that only the baseline report has under `Missing Benchmarks`.
  `--fail-on-new` and `--fail-on-missing` turn those into failures;
- lists `null` values, such as the statistics of an empty series, under
  `Unavailable Statistics` instead of comparing them;
- writes a Markdown summary to `--markdown-out`, or to stdout without it.

| Exit code | Meaning |
|---|---|
| `0` | No regression over the threshold. |
| `1` | A regression over the threshold, or a case that `--fail-on-new` or `--fail-on-missing` rejects. |
| `2` | Invalid input, such as an unreadable or malformed report, duplicate rows, or a requested metric missing from one report; or no metric value could be compared. The script prints `error: <reason>` on stderr. |

### Measurements that are not comparable

Two rows are comparable only when they were measured the same way: the same
`execution_mode`, `time_source` and `sampling_kind`, and, when either side uses
`batch-average` sampling, the same `calls_per_sample`. The script lists other
rows under `Not Comparable` with the field that differs. Jitter calibration can
choose a different batch size in two runs of the same binary, so this can happen
without any code change. When no metric value can be compared at all, the
script writes the summary and exits with `2`.

[Baseline deltas](#compare-cases-with-a-baseline) within one report follow the
same rule.

## Measured cases in JUnit and Allure

In JUnit and Allure reports, each measured case is one test case. For a passing
case, the reported time covers its calls, including calibration and warmup. In
Allure, a passing measured case also carries metrics, plots, the jitter
histogram and samples, and its named series as attachments; [Reports](reports.md)
lists them.

## Rules and errors

Codegen rejects these declarations:

| Declaration | Error |
|---|---|
| `bench` or `jitter` without exactly one non-empty name | `'bench' requires exactly one non-empty string argument` (`'jitter'` likewise) |
| `baseline` without `bench` or `jitter` | `'baseline' requires 'bench' or 'jitter' on the same declaration` |
| `items_per_call` without `bench` or `jitter` | `'items_per_call'/'ops_per_call' requires 'bench' or 'jitter' on the same declaration` |
| `items_per_call` with a value that isn't a plain positive integer | `'items_per_call' requires exactly one positive integer argument` |
| `bench_once`, `bench_warmup` or `bench_time` without `bench`, or with `test` or `jitter` | `'bench_once', 'bench_warmup', and 'bench_time' require 'bench' without 'test' or 'jitter' on the same declaration` |
| `bench_warmup` with a value that isn't a plain integer, 0 or more | `'bench_warmup' requires exactly one non-negative canonical decimal integer argument` |
| `bench_time` with another value than `"runner"` or `"supplied"` | `'bench_time' requires exactly one string argument: 'runner' or 'supplied'` |
| `bench_time("supplied")` without `bench_once` | `'bench_time("supplied")' requires 'bench_once'` |
| `bench_once` with a positive `bench_warmup` | `'bench_once' cannot be combined with positive 'bench_warmup'` |
| A measured case that returns `gentest::awaitable<T>` | `gentest::awaitable<T> return types are supported only for test cases, not bench or jitter cases` |

At run time:

- A failure message names the kind, the phase and the case, for example
  `benchmark setup failed for strings/concat: <reason>`. The case fails, the
  executable exits with `1`, and the next case runs unless you pass
  `--fail-fast`.
- Teardown runs after a failed setup or call.
- A failing check in the timed call, such as `EXPECT_EQ`, stops that case's
  measurement.
- A [static skip](assertions.md) (`skip("reason")`) prints a `[ SKIP ]` line
  and acquires no fixtures.
- Recording in the timed call fails the case with
  `recording is forbidden in a timed bench/jitter call; use fixture setup/teardown`
  (see [Runtime properties and structured records](recording.md)).
- A [hand-written case](runner_api.md) (`gentest::Case`) sets the same options
  in its fields: `is_benchmark` or `is_jitter`, `is_baseline`, `items_per_call`,
  and `benchmark`, a `gentest::BenchmarkOptions` with `execution`,
  `time_source` and `warmup_epochs`. The runner checks them as codegen checks
  the attributes. An invalid combination stops the run with exit code `1` and
  `invalid benchmark metadata for <name>: <reason>` before any fixture is set
  up.

Use `gentest_codegen`, the headers and the runtime from the same release.
Otherwise the build stops with
`#error "gentest_codegen output requires benchmark API version 1; use matching gentest headers/runtime"`,
or an older `gentest_codegen` rejects `--require-benchmark-api-version=1`.
