# Logging and threads

This page shows how a test hands work to its own threads, how to log from
them with `gentest::log()`, and where log messages go. It also covers the live
view that a run with async cases shows in a terminal.

## Log from a worker thread

Every running case has a test context. A thread you start has none, so give it
the case's context before it calls gentest:

```cpp
#include "gentest/test.h"

#include <future>
#include <thread>

using namespace gentest::asserts;

[[gentest::test("concurrency/adopt_and_log")]]
inline void adopt_and_log() {
    auto context = gentest::get_current_context();

    std::promise<int> result;
    auto              answer = result.get_future();
    std::thread       worker([context, &result] {
        auto lease = gentest::set_current_context(context);
        gentest::log("worker started");
        result.set_value(6 * 7);
    });
    worker.join();

    EXPECT_EQ(answer.get(), 42);
}
```

The test captures its context with `gentest::get_current_context()` and hands it
to the worker. The worker *adopts* it with `gentest::set_current_context(context)`,
which returns a `gentest::CurrentContextLease`. The adoption ends when the lease
is destroyed, so keep the lease in a named variable for as long as the worker
uses gentest. The worker's log messages belong to the case. The worker doesn't
assert: it sends its result back, and the test checks it.

## What a worker can do

An adopted context lets a worker report activity, not outcomes. It can:

- call `gentest::log()`;
- check `context.stop_requested()` or wait on `context.stop_token()`;
- publish progress through `gentest::currentNotifier()` (see
  [Notifications](notifications.md));
- call mocked methods, as long as the calls satisfy the test's expectations.

Everything that decides or describes the outcome belongs to the test's own
thread or coroutine:

- assertions and expectations (`EXPECT_*`, `ASSERT_*`, `gentest::expect*`,
  `gentest::require*`), even ones that pass;
- `gentest::fail()`, `gentest::skip()` and `gentest::xfail()`;
- `gentest::diagnostic_context(...)` scopes;
- mock configuration (`EXPECT_CALL`, `make_nice`, changing an expectation), and
  mock calls that fail, such as an unexpected call;
- `gentest::record_property()` and `gentest::record_data()`;
- `gentest::recordTimingSamples()`, `gentest::setBenchmarkDuration()` and
  `gentest::currentMeasurement()`.

Any of these on an adopted context aborts the process with a message that names
the call, for example:

```text
gentest: fatal: assertion/expectation called from an adopted test context.
        Worker/adopted contexts may log and observe stop only; report results back and assert on the owning test.
```

A thread without an adopted context, or one that adopts a context after its
case has ended, can't log, check or record. Even `gentest::log()` aborts there:

```text
gentest: fatal: log called without an active test context.
        Did you forget to set the current context in this thread/coroutine?
```

On a thread without a context, calls that report nothing work:
`get_current_context()` returns an empty context, `currentNotifier()` returns a
disabled handle, and a mock call that matches its expectations runs normally. A
failing mock call aborts with
`gentest: fatal: assertion/expectation recorded without an active test context.`
(see [Mocks](mock_generation.md)).

## When the case ends

When the case body and its local fixtures have finished, gentest requests stop
on the case's context. It then waits until every lease on that context has been
destroyed, and only then reports the case. Shared fixture setup and teardown end
the same way.

Work that can outlive the case body, such as a thread owned by a suite fixture,
can watch `context.stop_requested()` or `context.stop_token()` and finish
early. The runner also requests stop when it cancels an async case, for example
after a failure under `--fail-fast`.

> [!WARNING]
> If a worker never destroys its lease, for example a detached or stuck thread,
> the case never finishes and the run hangs.

A `std::stop_callback` registered on `context.stop_token()` runs as if on an
adopted thread: it can log, but it must not assert.

## Coroutines and async cases

In an [async case](async.md), gentest sets the context for the case's own
coroutines and for results that a backend delivers through a continuation. You
don't adopt anything there. Adopt the context only on threads you start (or a
backend's threads) that call `gentest::log()`, check for stop or publish
progress. While an async
case has adopted work, gentest keeps waiting for that work instead of reporting
the case as unable to resume.

## Context API

All of these are in `gentest/context.h`, which `gentest/test.h` includes, and in
`import gentest;`.

| Name | Meaning |
| --- | --- |
| `gentest::get_current_context()` | Returns the calling thread's `gentest::CurrentContext`. It is empty (`false` in a boolean test) on a thread without a context. |
| `gentest::set_current_context(context)` | Adopts `context` on the calling thread and returns a `gentest::CurrentContextLease`. The result is `[[nodiscard]]`. |
| `gentest::CurrentContextLease` | Ends the adoption when destroyed and restores the context the thread had before. It can't be copied or moved. |
| `CurrentContext::stop_token()`, `CurrentContext::stop_requested()` | Observe the stop that gentest requests when the case ends or is cancelled. |

`gentest::CurrentToken`, `gentest::get_current_token()` and
`gentest::set_current_token()` are other names for `CurrentContext`,
`get_current_context()` and `set_current_context()`.

## Logging

`gentest::log(message)` records `message` on the current case and passes it to
every registered log sink right away. Where it ends up depends on the sinks and
the output options:

- The default sink prints each message as a line on stdout. With
  `--report-format=json` or `csv`, it prints to stderr instead, so stdout holds
  only the measured report (see [Benchmarks](benchmarks.md)).
- With `--log-output=failures`, the default for `--output=compact` and
  `--output=failures-only`, the default sink stays quiet. The runner prints a
  failed case's messages to stderr instead. See
  [Running, debugging, and replaying tests](runner_workflows.md) for both options.
- Allure results attach each case's messages as `logs` (see [Reports](reports.md)).
- Sinks you add receive every message immediately, under every output option.

Logging never renews a progress watchdog; see [Notifications](notifications.md).

## Log sinks

A log sink receives every `gentest::log()` message. This `main()` copies all
messages into a file:

```cpp
// main.cpp
#include "gentest/log_sink.h"
#include "gentest/runner.h"

#include <fstream>

int main(int argc, char **argv) {
    std::ofstream file("test-logs.txt");
    auto          sink = gentest::add_log_sink(gentest::make_ostream_log_sink(file));
    const int     rc   = gentest::run_all_tests(argc, argv);
    sink.remove();
    return rc;
}
```

Because the program has its own `main()`, link `gentest::gentest` instead of
`gentest::gentest_main` (see [Embedding the runner](runner_api.md)):

```cmake
target_link_libraries(my_tests PRIVATE gentest::gentest gentest::gentest_runtime)
```

The sink functions are in `gentest/log_sink.h`, which `gentest/context.h`
includes:

| Function | What it does |
| --- | --- |
| `gentest::add_log_sink(sink)` | Registers a `std::shared_ptr<gentest::LogSink>` and returns a `gentest::LogSinkHandle`. A null pointer registers nothing and returns an inactive handle. |
| `gentest::make_ostream_log_sink(stream)` | Creates a sink that writes each message as one line to a `std::ostream`. It keeps a reference to the stream: remove the sink before the stream is destroyed. |
| `handle.remove()`, `gentest::remove_log_sink(handle)` | Unregisters the sink. Returns whether a sink was removed. |
| `handle.active()` | Returns whether the sink is registered. |
| `gentest::remove_all_log_sinks()` | Removes every sink, including the default stdout sink. |
| `gentest::restore_default_log_sink()` | Removes every sink and registers the default stdout sink again. |

To write your own sink, derive from `gentest::LogSink` and implement
`void write(std::string_view message) noexcept`. Keep these rules in mind:

- Adopted workers can log at the same time, so `write()` may run on several
  threads at once. Synchronize inside it. The stream sink from
  `make_ostream_log_sink()` already does.
- The sink list has no lock. Add and remove sinks only while no other thread
  can call `gentest::log()`, for example in `main()` before `run_all_tests()`.
- A `LogSinkHandle` is a removal token, not a guard: destroying it leaves the
  sink registered. Move-assigning a handle onto one that is active aborts
  the process (`gentest: fatal: move-assigned over an active LogSinkHandle; call remove() first.`).

## Live output for async runs

When the selected cases include [async cases](async.md) and stdout is a
terminal, the runner shows a live view. Each running async case has a row with
its state: `RUNNING`, `YIELDED` or `SUSPENDED`, and for a waiting case, the
source location it waits at. Finished cases print their result line as usual.

Messages that async cases send to the default sink appear as the last lines of
their row instead of scrolling past; messages from other cases print above the
view. `--async-log-tail=N` sets how many lines a row shows (default 5; `0` shows
none). A finished row keeps only the count, such as `:: 2 log(s)`. Sinks you add
receive every message. To keep every message in the terminal, turn the live
view off.

The live view runs only with `--output=full` (the default) and
`--log-output=all`. These environment variables also control it:

| Variable | Effect |
| --- | --- |
| `GENTEST_ASYNC_LIVE=1` (or `true`, `yes`, `on`) | Shows the live view even when `CI`, `CODEX_CI` or `TERM=dumb` is set. Stdout must be a terminal either way; on Windows, one that accepts virtual terminal sequences. |
| `GENTEST_ASYNC_LIVE=0` (or `false`, `no`, `off`) | Plain output. |
| `GENTEST_NO_ASYNC_LIVE=1` (or `true`, `yes`, `on`) | Plain output. |
| `CI` or `CODEX_CI` with any non-empty value | Plain output, unless `GENTEST_ASYNC_LIVE=1`. |
| `TERM=dumb` | Plain output, unless `GENTEST_ASYNC_LIVE=1`. |

Flag values are all lowercase or all uppercase: `on` and `ON` work, `On`
doesn't. In plain output, async cases print their result lines like other
cases.

The same conditions turn off color: stdout that isn't a terminal, `CI`,
`CODEX_CI`, `TERM=dumb`, `GENTEST_NO_ASYNC_LIVE=1` or `GENTEST_ASYNC_LIVE=0`.
`GENTEST_ASYNC_LIVE=1` doesn't turn color back on. `--no-color`, `NO_COLOR` and
`GENTEST_NO_COLOR` turn off color alone; see [Command-line options](command_line.md).
