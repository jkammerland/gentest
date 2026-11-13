# Embedding the Gentest runner

Use `gentest::run_cases` when an embedding program owns a `gentest::Case` table and wants Gentest's normal CLI selection,
execution, reporting, fixture, benchmark, and jitter behavior without adding those cases to the global registry.

```cpp
#include "gentest/runner.h"

#include <cstddef>
#include <span>
#include <vector>

void check_service(void *) {
    // The Gentest assertion APIs are available while this callable runs.
}

static const gentest::Case cases[] = {{
    .name = "embedded/check_service",
    .fn = &check_service,
    .file = __FILE__,
    .line = __LINE__,
    .fixture_lifetime = gentest::FixtureLifetime::None,
    .suite = "embedded",
}};

int main(int argc, char **argv) {
    std::vector<const char *> args;
    args.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i)
        args.push_back(argv[i]);
    return gentest::run_cases(cases, args);
}
```

`run_cases` is also exported by `import gentest;`. Its argument parsing is identical to `run_all_tests`: an optional first program
name is ignored, null pointers are skipped, and an empty argument span requests the default run.

The runner makes a private, shallow copy of the descriptors and sorts it by name, file, and line, matching registered-case order.
It never mutates the caller's table or adds it to `registered_cases()`. Keep the callables and all storage referenced by `Case`
string views and spans alive until `run_cases` returns.

Return codes match `run_all_tests` for the same cases and arguments: `0` means the requested operation completed successfully
(including an ordinary filter that selects no cases), `1` reports invalid CLI input or a test/runtime failure, and `3` means an
`--run` case was not found. `--run` prefers a full name, then accepts a unique suffix among cases matching `--kind`;
ambiguous suffixes are errors. Benchmark- or jitter-only filters that match nothing retain their existing failure semantics.

Options that take one value (`--run`, `--filter`, `--kind`, `--seed`, `--repeat`, `--output`, `--log-output`, `--time-unit`,
`--report-format`, `--junit`, `--allure-dir`, `--records`, `--skip-return-code`, `--async-log-tail`, the `--bench-*` settings and
`--jitter-bins`) may appear once; a repeated one is rejected as a CLI error (exit `1`) instead of choosing a winner. Only
`--tag`, `--exclude-tag` and `--exclude` accumulate. Out-of-range counts are rejected too, not adjusted: `--repeat` accepts
1–1000000, `--bench-epochs` requires at least 1, and `--jitter-bins` accepts 1–10000.

`--skip-return-code=N` (4–255) returns `N` when every selected case skips and the run has no failures. Expected failures (XFAIL)
retain success status. Discovered ordinary CTest entries use `--skip-return-code=77` with `SKIP_RETURN_CODE=77`, so runtime and
attribute skips appear as skipped. Fixture teardown and report-write errors return failure even when the case skips.

A successful execution with `--junit=<path>` writes a report for that invocation, including zero-test selections and empty case
tables. Use `--fail-on-empty` when CI requires a nonempty selection. Listing and preview operations do not write reports.

Report outcome mapping:

| Outcome | JUnit | Allure `status` |
| --- | --- | --- |
| pass | no child element | `passed` |
| fail, unexpected pass | `<failure>`, counted in `failures` | `failed` |
| skip, expected failure | `<skipped>`, counted in `skipped` | `skipped` (expected failures add an `xfail` label) |
| blocked | `<error type="blocked">`, counted in `errors` | `broken`, with `blocked` and `blocked_reason` labels |
| runner or fixture infrastructure error | `<system-err>`, counted in `errors` | separate `failed` result under `gentest/infra` |

A blocked case did not run and makes the run exit `1`, so it is reported as an error, never as a skip. JUnit `time` values are
seconds in fixed-point decimal notation and, like the counts, do not depend on the process locale. Text that is not valid UTF-8
is replaced in both formats (`?` in JUnit, U+FFFD in Allure).

Manual benchmarks may set source-level execution policy with the
`Case::benchmark` field:

```cpp
.benchmark = {
    .execution = gentest::BenchmarkExecution::Once,
    .time_source = gentest::BenchmarkTimeSource::Supplied,
    .warmup_epochs = 0,
},
```

Omit the field for ordinary calibrated runner timing. Invalid combinations are
rejected before fixture setup. See [benchmark measurements](benchmark_measurements.md)
for publication, count normalization and snapshot contracts.

## Notifications and source events

Pass a `RunConfiguration` as the final argument to `run_cases` or either
`run_all_tests` overload to install a notifier and an optional shared
`FrameworkEventVisitor`. Existing overloads remain available. The configuration
covers shared fixtures, case occurrences, adopted work, and final cleanup;
notification infrastructure failures make the run fail even for an expected
test failure. See [notifications and progress watchdogs](notifications.md) for
complete setup, source-hook semantics, explicit quiet periods, and Linux process
supervision.
