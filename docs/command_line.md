# Command-line options

This page lists every option a gentest test executable accepts, by task, then
the exit codes and the environment variables the runner reads. The options are
the same whether the executable links `gentest::gentest_main` or passes its
arguments to `gentest::run_all_tests()` or `gentest::run_cases()` (see
[Embedding the runner](runner_api.md)).

## Common commands

| Command | What it does |
| --- | --- |
| `./my_tests` | Runs every case except death tests. |
| `./my_tests --list-tests` | Prints every case name. |
| `./my_tests --run=math/add` | Runs one case. |
| `./my_tests --filter='math/*' --kind=test` | Runs the tests whose names start with `math/`. |
| `./my_tests --tag=fast --exclude-tag=slow` | Runs the cases tagged `fast` that are not also tagged `slow`. |
| `./my_tests --output=failures-only --junit=results.xml` | Prints only failures and writes a JUnit report. |
| `./my_tests --include-death --run=crash/fatal_path` | Runs one death test in this process. |

Options that take a value accept both `--name=value` and `--name value`.
`./my_tests --help` prints the runtime version (for example `gentest v2.0.0`)
and a short summary of the options.

## Listing cases

| Option | Prints |
| --- | --- |
| `--help` | The version line and the option summary. |
| `--list-tests` | Every case name, one per line: tests, benchmarks and jitter cases. |
| `--list` | Every case with its metadata and source location. |
| `--list-json` | The inventory as one JSON array; see [Test inventory](test_inventory.md). |
| `--list-tags` | Every distinct tag, in ASCII lowercase and sorted. |
| `--list-death` | The death tests, except static skips. |
| `--list-benches` | The benchmark and jitter cases. |
| `--list-selected` | The names the selection options select, without running anything; see [Test selection](test_selection.md#previewing-a-selection). |

`--list` adds a `[gentest:...]` section when a case has tags, requirements, a
static skip or an owner, followed by the declaration's file and line:

```text
math/add [gentest:tags=fast] (cases.hpp:16)
math/later [gentest:skip=not ready] (cases.hpp:31)
```

The listing options read the whole inventory, print in name order, and ignore
the selection options; only `--list-selected` applies them. `--list-selected`
and `--list-tags` reject any other listing option, and `--help` takes precedence
over all of them.

## Selecting cases

| Option | Effect |
| --- | --- |
| `--run=<name>` | Selects one case by its full name or a unique name suffix. |
| `--filter=<pattern>` | Selects the cases whose names match a wildcard pattern (`*`, `?`). Ignored when `--run` is given. |
| `--kind=<kind>` | Keeps only `test`, `bench` or `jitter` cases. The default, `all`, keeps every kind. |
| `--tag=<tag>` | Keeps the cases that have any of the given tags. Repeatable. |
| `--exclude-tag=<tag>` | Removes the cases that have this tag. Repeatable. |
| `--exclude=<pattern>` | Removes the cases whose names match this pattern. Repeatable. |
| `--include-death` | Lets death tests run. Without it, the runner removes them from every selection. |
| `--fail-on-empty` | Makes an empty selection an error instead of a successful run of zero cases. |

[Test selection](test_selection.md) explains how these options combine, how
names and tags match, and what happens when nothing is selected. `--kind` also
accepts the spellings `tests`, `benches`, `benchmark`, `benchmarks` and
`jitters`.

## Running cases

One execution of a case is an **occurrence**. A plain run gives each selected
case one occurrence; `--repeat=3` gives each selected test three occurrences in
the same process. The summary line counts occurrences.

| Option | Effect |
| --- | --- |
| `--repeat=N` | Runs the selected tests `N` times in one process (1 to 1000000, default 1). Benchmarks and jitter cases run once. |
| `--shuffle` | Shuffles the order of tests inside each fixture group. |
| `--seed=N` | Sets the base seed for `--shuffle`. Without it, `--shuffle` picks a random seed. Without `--shuffle`, it has no effect. |
| `--fail-fast` | Stops after the first case that fails, passes unexpectedly (XPASS) or is blocked. Later tests, benchmarks and jitter cases don't run. |
| `--break-on-failure` | Raises a debugger trap when an assertion or expectation fails, so an attached debugger stops there. Without a debugger, the trap usually ends the process. |
| `--skip-return-code=N` | Exits with `N` (4 to 255) when every selected case skipped. |

[Running, debugging, and replaying tests](runner_workflows.md) shows how to
replay a shuffled run and debug a failing case. Skipping is described on
[Assertions](assertions.md).

## Console output

| Option | Effect |
| --- | --- |
| `--output=<mode>` | `full` (default) prints a line per case; `compact` prints one progress symbol per case; `failures-only` prints only failures. All three end with the summary. |
| `--log-output=<mode>` | `all` writes every `gentest::log()` message to the default log sink as it happens; `failures` holds a case's messages and prints them only if the case fails. The default is `failures` with `--output=compact` or `--output=failures-only`, and `all` otherwise. |
| `--no-color` | Turns off colored output. |
| `--async-log-tail=N` | Shows the last `N` log lines under each running coroutine case in live output (default 5; 0 shows none). |

See [Running, debugging, and replaying tests](runner_workflows.md#control-console-output)
for the output modes and log capture, and [Logging and threads](logging_and_threads.md)
for live output of coroutine cases.

## Reports

| Option | Effect |
| --- | --- |
| `--junit=<file>` | Writes a JUnit XML report. |
| `--allure-dir=<dir>` | Writes Allure result files into a directory. |
| `--github-annotations` | Prints a GitHub Actions `::error` annotation for each failure. It is on by default when `GITHUB_ACTIONS` is set. |
| `--records=<dir>` | Exports runtime properties and records into a fresh `run-...` directory inside `<dir>`. |

See [Reports](reports.md) for the report formats and
[Runtime properties and records](recording.md) for `--records`.

## Measured runs

These options control benchmarks and jitter cases, the measured cases. The
`--bench-*` timing settings apply to jitter cases too. A benchmark's own
attributes, such as `bench_once` or `bench_warmup(...)`, take precedence over
them.

| Option | Effect |
| --- | --- |
| `--report-format=<format>` | `table` (default), `markdown` (or `md`), `csv` or `json`. `csv` and `json` require a selection of measured cases only. |
| `--time-unit=<unit>` | `auto` (default) picks a unit for each table cell; `ns` prints nanoseconds. |
| `--bench-epochs=N` | Number of measured epochs per case (at least 1, default 12). |
| `--bench-warmup=N` | Warmup epochs before measuring (default 1). |
| `--bench-min-epoch-time-s=<sec>` | Calibration doubles the calls per epoch until one epoch takes at least this long (default 0.01). |
| `--bench-min-total-time-s=<sec>` | Keeps measuring until this much measured time has accumulated, even past `--bench-epochs` (default 0). |
| `--bench-max-total-time-s=<sec>` | Stops measuring a case after this much time once the minimum total is reached, even before `--bench-epochs` (default 1; 0 removes the limit). |
| `--jitter-bins=N` | Histogram bins in jitter reports (1 to 10000, default 10). |
| `--bench-table` | Has no effect: `--report-format` decides how benchmark results print. Rejected with `--kind=jitter`. |

With `--report-format=csv` or `json`, stdout carries only the report.
[Benchmarks](benchmarks.md) explains measurement, the report formats and that
stdout rule.

## Option rules

- A value can't be empty: `--tag=` and a trailing `--tag` are errors.
- `--tag`, `--exclude-tag` and `--exclude` accumulate when repeated. Every other
  option that takes a value may appear only once; a second `--run`, `--kind`,
  `--repeat` or similar is an error, not a "last one wins".
- A comma is part of the value, not a list separator. Repeat the option to give
  several tags or patterns.
- Counts and seeds are non-negative decimal integers: no sign, no `0x` prefix.
  The `--bench-*-time-s` values are finite, non-negative numbers of seconds.
- Values outside the ranges above are rejected, never clamped.
  `--bench-min-total-time-s` can't exceed `--bench-max-total-time-s` unless the
  maximum is 0.
- Unknown options and positional arguments are errors, for example
  `error: unknown option '--bogus'` and `error: unexpected argument 'stray'`.
- Removed options such as `--run-test` fail with a message that names the
  replacement. [DEPRECATIONS.md](../DEPRECATIONS.md) lists them.

An invalid command line exits with `1` before the runner runs a case or writes a
report.

## Exit codes

| Code | Meaning |
| --- | --- |
| `0` | Every selected occurrence passed, skipped, or failed as expected (XFAIL). An empty selection without `--fail-on-empty`, `--help` and the listing options also return `0`; `--list-selected` returns the same selection errors as a run. |
| `1` | A case failed, passed unexpectedly (XPASS) or was blocked; shared fixture setup or teardown failed; a report could not be written; a notification could not be delivered (see [Notifications](notifications.md)); or the command line was invalid. Errors found before any case runs also exit with `1`: every [selection error](test_selection.md#selection-errors) except `Case not found` (for example a `--filter` that matches no case of the kind given by `--kind=bench` or `--kind=jitter`; `--kind=bench` alone with no benchmarks exits with `0`), `--report-format=csv` or `json` with a selection that contains tests, and invalid benchmark options in a [hand-written case](runner_api.md). |
| `3` | `--run` matched no case. The runner prints `Case not found: <name>`. |
| `4` to `255` | The value of `--skip-return-code` when every selected occurrence skipped and nothing failed. Without that option, such a run exits with `0`. |

An XFAIL keeps the run successful and prevents the `--skip-return-code` status:
a run where every case either skipped or failed as expected exits with `0`.

[CTest integration](discover_tests.md) registers each case with
`--skip-return-code=77` and the CTest property `SKIP_RETURN_CODE 77`, so skipped
cases show as skipped in CTest.

## Environment variables

| Variable | Effect |
| --- | --- |
| `NO_COLOR`, `GENTEST_NO_COLOR` | Any nonempty value turns off colored output, like `--no-color`. |
| `GITHUB_ACTIONS` | Any nonempty value turns on `--github-annotations`. GitHub Actions sets it. |
| `CI`, `CODEX_CI` | Any nonempty value turns off color and the live output for coroutine cases. |
| `TERM` | `TERM=dumb` turns off color and the live output for coroutine cases. |
| `GENTEST_ASYNC_LIVE`, `GENTEST_NO_ASYNC_LIVE` | Force the live output for coroutine cases on or off; see [Logging and threads](logging_and_threads.md). Turning it off this way also turns off color. |
| `GENTEST_NOTIFY_FDS`, `GENTEST_NOTIFY_ID` | Read by `gentest::notifierFromEnvironment()` on Linux, not by the runner itself; see [Notifications](notifications.md). |

The runner also turns off color when stdout is not a terminal.

## Options for gentest's own tools

The death-test script (`GentestCheckDeath.cmake`) that `gentest_discover_tests()` registers passes
`--gentest-death-marker=<file>`, which records when the test body starts. The
runner accepts it only with `--include-death` and a `--run` that selects one
death test. You don't need it when running tests yourself; see
[Death tests](death_tests.md).
