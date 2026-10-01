# Jitter sampling

Gentest uses timer-overhead estimates and switches to batch sampling for very
small operations to amortize clock reads. See the
[benchmark and jitter examples](../README.md#benchmarks-and-jitter) for declarations
and CLI usage.

## Reading the results

The summary shows `Sampling` (`per-call` or `batch-average`) and `Calls/sample`.
When a sample averages more than one call, the report warns that its histogram,
percentiles, standard deviation and maximum describe batch averages;
individual-call latency spikes may be hidden. A batch of one call reports
per-call samples without an averaging warning.

`Max batch avg` labels the maximum in a batch-only summary; mixed sampling
summaries use `Max sample`. Adding a timer around each call would bring clock
overhead back into those very small measurements.

Main timing columns remain normalized per item. `Calls/sample` counts function
invocations and is separate from `Items/call`, which counts logical items within
each function invocation.

## Exported metadata

JSON/CSV summary rows and Allure metrics/raw samples include `mode`,
`sample_kind`, and `calls_per_sample`. `mode` identifies the timing path (`batch`
or `per-call`); `sample_kind` identifies whether samples average multiple calls
(`batch-average` or `per-call`). A one-call batch retains `mode: batch` while
reporting `sample_kind: per-call`.

`sampling_warning` explains the averaging limitation when it applies. JSON uses
`null` when samples do not average multiple calls; Allure metrics omit the warning
in that case. Warnings stay inside the structured JSON/CSV output.
Existing timing field names and values remain compatible.
