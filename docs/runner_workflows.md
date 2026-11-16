# Running, debugging, and replaying tests

This page covers the everyday loop after a test fails: rerun the failing case,
stop in a debugger at the failing assertion, cut the console output down to the
failures, replay a shuffled run, and repeat cases through CTest. Every option
used here is listed on [Command-line options](command_line.md).

## Rerun a failing case

At the end of a run, the runner prints a `Rerun:` command on stderr for each
case in the summary's `Failed tests:` list:

```text
Rerun: './my_tests' '--run=math/fails' '--kind=test' '--no-color'
```

The command selects that one case by its full name and kind and runs it once.
It adds `--include-death` for a death test. A case whose name another case
shares gets no command, because `--run` can't select it alone.

To focus on a case yourself, pass its exact name. Quote parameterized names so
the shell passes them unchanged:

```bash
./my_tests --run='math/add(2, "two words")' --kind=test
```

The printed commands use POSIX shell quoting on Unix. On Windows they are
PowerShell commands that run the executable through `Start-Process`, so
embedded quotes survive, and set `$LASTEXITCODE` to its exit code.

The runner needs the executable path to build these commands.
`gentest::gentest_main` passes it; your own `main()` should pass it as the
first argument (see [Embedding the runner](runner_api.md)).

## Stop in a debugger

```bash
gdb --args ./my_tests --run='math/add(2, "two words")' --break-on-failure
```

`--break-on-failure` traps when an assertion or expectation fails, before a
fatal assertion unwinds the test, so the assertion call is on the stack.
Continue in the debugger to finish normal failure reporting. Assertions in
expected-failure cases trap too.

MSVC-compatible Windows builds use `__debugbreak()`. Platforms with `SIGTRAP`,
including Linux and macOS, raise that signal. Other platforms reject the option
with `error: --break-on-failure is unsupported on this platform`. Without a
debugger or a signal handler, the trap may end the process.

## Control console output

```bash
./my_tests --output=compact
./my_tests --output=failures-only
./my_tests --log-output=failures
```

`--output` controls what the runner prints for each case:

| Mode | Output |
| --- | --- |
| `full` | A result line for every case, failure details, and the final summary. The default. |
| `compact` | One progress symbol per case (`.` pass, `s` skip, `x` expected failure, `F` failure, blocked or unexpected pass), failure details, and the summary. |
| `failures-only` | Details of failed, blocked and unexpectedly passing cases, followed by the summary. |

`compact` and `failures-only` default to `--log-output=failures`. The runner
then holds the `gentest::log()` messages that the default log sink would print,
and writes them to stderr under `Captured logs:` only when a case, fixture or
measured case fails, is blocked or passes unexpectedly. Messages logged by a
worker that [adopted](logging_and_threads.md) a case's context belong to that
case. Each [occurrence](command_line.md#running-cases) has its own capture.

Use `--log-output=all` to print every message as it happens, also with
`compact` or `failures-only`. Either way:

- Log sinks that your program installs receive every message immediately.
- JUnit, Allure and runtime records are unaffected; Allure log attachments
  include the messages of cases that passed.
- Capture covers `gentest::log()` only, not your program's own writes to stdout
  or stderr.
- The live output for coroutine cases is off with `compact`, `failures-only` or
  `--log-output=failures`; see [Logging and threads](logging_and_threads.md).

With `--report-format=json` or `csv`, stdout contains only the measured report,
and failure details and captured logs go to stderr; see
[Benchmarks](benchmarks.md).

## Shuffle and repeat

```bash
./my_tests --kind=test --filter='math/*' --shuffle --repeat=20
./my_tests --kind=test --filter='math/*' --shuffle --seed=123 --repeat=20
```

`--repeat=N` runs the selected tests `N` times in one process; each pass is an
iteration. `--shuffle` picks a random base seed unless `--seed`
gives one. With `--shuffle`, every iteration prints its seed, its number, the
base seed and a `Replay iteration:` command. For example,
`./my_tests --kind=test --filter='math/*' --shuffle --seed=123 --repeat=2`
prints these lines among its results:

```text
Shuffle seed: 123 (iteration 1/2; base seed 123)
Replay iteration: './my_tests' '--kind=test' '--filter=math/*' '--shuffle' '--seed=123' '--repeat=1'
Shuffle seed: 11400714819323198608 (iteration 2/2; base seed 123)
Replay iteration: './my_tests' '--kind=test' '--filter=math/*' '--shuffle' '--seed=11400714819323198608' '--repeat=1'
```

The same base seed and selection reproduce the same sequence of orders. Each
iteration shuffles with its own seed but keeps the fixture groups: within each
suite, the tests without a shared fixture run first, then each suite fixture
group, then each global fixture group, and groups never interleave.

The first iteration uses the base seed. A printed iteration seed replays that
iteration alone with `--shuffle --seed=<iteration-seed> --repeat=1`; the printed
`Replay iteration:` command does that and keeps the other selection and
execution options.

A replay reproduces the order for the same inventory, but not the state. All
iterations of one invocation share its suite and global fixtures; replaying one
iteration starts a separate run without the state that earlier iterations built
up. To investigate a failure that depends on such state, keep the original base
seed and repeat count. Use CTest repetition when each repetition should start a
fresh process.

## CTest workflows

After [registering the cases with CTest](discover_tests.md), CTest can run each
case as a separate process:

```bash
ctest --test-dir build --parallel 8 --output-on-failure
ctest --test-dir build --rerun-failed --output-on-failure
ctest --test-dir build --repeat until-fail:50 --output-on-failure
```

`--repeat until-fail:N` runs each selected entry up to `N` times and stops
repeating an entry once it fails. Add `-R '<regex>'` (a regular expression, not
a wildcard pattern like `--filter`) or `-L <label>` to pick entries; every case
tag is a label.

A CTest repeat starts a fresh executable for each occurrence. The runner's
`--repeat=N` stays in one process, with a fresh context for each occurrence and
the same shared fixtures.
