# Running, debugging, and replaying tests

Use an exact case name to focus a failing test. Quote parameterized names when
passing them through a shell:

```sh
./my_tests --run='math/add[value=2, label="two words"]' --kind=test
```

The runner prints a `Rerun:` command for each uniquely named failing selected
case, using its full inventory name and kind. These commands use POSIX shell
quoting on Unix and PowerShell quoting on Windows, including executable paths with spaces and
apostrophes. They select one case without repeating it. An embedding program
using `run_cases` or the span overload of `run_all_tests` should pass its
executable path as the optional first argument to enable these commands.

Windows commands use `Start-Process` with a single encoded argument string so
embedded quotes also survive Windows PowerShell's native argument handling.
They set `$LASTEXITCODE` to the executable's exit code.

## Debugger breaks

```sh
gdb --args ./my_tests --run='math/add[value=2]' --break-on-failure
```

`--break-on-failure` traps while recording a failing assertion or expectation,
before its failure is stored or a fatal assertion unwinds the test. The test's
assertion call remains on the stack. Continue in the debugger to finish normal
failure recording and reporting. Assertions in expected-failure cases also
trap; the option responds to the assertion, rather than the final case outcome.

MSVC-compatible Windows builds use `__debugbreak()`. Platforms with `SIGTRAP`,
including Linux and macOS, raise that signal. Other platforms reject the option.
A trap may terminate the process when no debugger or signal handler handles it.

## Output and captured logs

```sh
./my_tests --output=compact
./my_tests --output=failures-only
./my_tests --log-output=failures
```

`--output` controls ordinary test output:

| Mode | Output |
| --- | --- |
| `full` | Named result lines, failure details, and the final summary; the default. |
| `compact` | Progress symbols (`.` pass, `s` skip, `x` expected failure, `F` failure/blocked/unexpected pass), failure details, and the summary. |
| `failures-only` | Failure, blocked, and unexpected-pass details, followed by the summary. |

Compact and failures-only modes default to `--log-output=failures`. This buffers
`gentest::log()` messages for the default sink and prints a failed case's captured
messages to stderr. Successful, skipped, and expected-failure cases keep their
messages out of the console. Fixture failures and failed measured cases also
print their captured lifecycle messages. Adopted workers' logs belong to their
owning case. Each repeated occurrence has its own capture.

Use `--log-output=all` to stream every message to the default sink, including
with compact or failures-only output. Application-installed log sinks receive
every message immediately under either policy. Runner policies are restored
when an invocation ends, including after failure. Console policies preserve the
case data exported by JUnit, Allure, and runtime records. When Allure is enabled,
its log attachments include successful cases' captured messages.

Async live rendering is disabled for compact/failures-only output or failure
log capture. Measured tables remain controlled by `--report-format`. JSON/CSV
stdout contains only the report; failure details and captured logs use stderr.
Capture covers `gentest::log()`, so application writes to stdout/stderr and
application-installed sinks should choose their own destinations accordingly.

## Shuffle and repeat replay

```sh
./my_tests --kind=test --filter='math/*' --shuffle --repeat=20
./my_tests --kind=test --filter='math/*' --shuffle --seed=123 --repeat=20
```

`--shuffle` chooses a random base seed unless `--seed` supplies one. Every test
iteration prints its seed, its iteration number, the base seed, and a
`Replay iteration:` command. Repeating the same base seed and selection
reproduces the same sequence of orders. Each iteration rebuilds the plan with
a distinct seed, while retaining the fixture groups: free/local tests first,
then each suite fixture group, then each global fixture group, without
interleaving groups. Small groups can have the same order for different seeds.

The first iteration uses the base seed. For zero-based iteration `i`, the seed
is `base + 0x9e3779b97f4a7c15 * i`, modulo `2^64`. A printed iteration seed works
directly with `--shuffle --seed=<iteration-seed> --repeat=1`. The printed replay
command preserves the original selection and execution options, replacing
only seed and repetition.

Replay reproduces ordering for the same inventory. Runner repetitions share
the run's suite/global fixtures; replaying one iteration starts a new run and
does not recreate state accumulated in earlier iterations. To investigate a
stateful failure, retain the original base seed and repeat count. Use CTest
repetition when each repetition should start a fresh process.

## CTest workflows

After [discovering cases](discover_tests.md), CTest can schedule each case as a
separate process:

```sh
ctest --test-dir build --parallel 8 --output-on-failure
ctest --test-dir build --rerun-failed --output-on-failure
ctest --test-dir build --repeat until-fail:50 --output-on-failure
```

`--rerun-failed` uses CTest's most recent failed-test log for that build
directory. `--parallel` schedules independent CTest entries concurrently;
choose a level appropriate for the tests' resource requirements.
`--repeat until-fail:N` runs each selected CTest entry up to `N` times, stopping its
repetitions when it fails. Add `-R '<CTest-name-regex>'` to select entries for a
parallel or repeated run. CTest uses regular expressions for `-R`; the runner's
`--filter` uses wildcard patterns.

CTest repeats launch a fresh executable for each occurrence. The runner's
`--repeat=N` repeats the selected tests inside one invocation, using fresh
per-case contexts and retaining the run's shared fixtures.
