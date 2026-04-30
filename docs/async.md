# Async Tests

Async tests are normal `[[using gentest: test]]` cases whose return type is
`gentest::async_test<T>`. Gentest resumes them cooperatively on one runner
thread, so suspended tests can wait while other ready tests in the same fixture
group continue.

```cpp
#include "gentest/attributes.h"
#include "gentest/runner.h"

using namespace gentest::asserts;

[[using gentest: test("async/basic")]]
gentest::async_test<void> basic_async_test() {
    co_await gentest::async::yield();
    EXPECT_TRUE(true);
}
```

With named modules, `import gentest;` exports the same async surface.

## Return Values

Top-level test return values are ignored by the runner. Return values are useful
when one async operation awaits another.

```cpp
gentest::async_test<int> compute_value() {
    co_await gentest::async::yield();
    co_return 42;
}

[[using gentest: test("async/value")]]
gentest::async_test<void> uses_value() {
    int value = co_await compute_value();
    EXPECT_EQ(value, 42);
}

[[using gentest: test("async/top_level_value")]]
gentest::async_test<int> top_level_value_is_discarded() {
    co_return 7;
}
```

Failures, skips, blocked dependencies, and exceptions from an awaited
`async_test<T>` propagate through the `co_await`.

Parameterized and template tests work the same way.

```cpp
[[using gentest: test("async/parameter"), parameters(value, 1, 2, 3)]]
gentest::async_test<void> parameterized_async(int value) {
    co_await gentest::async::yield();
    EXPECT_TRUE(value >= 1);
}

template <typename T>
[[using gentest: test("async/template"), template(T, int, long)]]
gentest::async_test<void> templated_async() {
    co_await gentest::async::yield();
    EXPECT_TRUE(std::is_integral_v<T>);
}
```

## Suspension Primitives

| Operation | Use it for | Live status | If it cannot resume |
| --- | --- | --- | --- |
| `gentest::async::yield()` | Move this test to the back of the ready queue | `YIELDED` | It is already queued to resume |
| `manual_event::wait(reason)` | Wait until another case or thread calls `set()` | `SUSPENDED` | `FAIL`: `cannot resume async test; suspended at file:line: reason` |
| `completion_source::wait(reason)` | Wait for a one-shot operation | `SUSPENDED` | Depends on how the source completes |
| `completion_source::fail_unresumable(reason)` | Mark an external dependency as impossible | `BLOCKED` | Fails the run and reports the reason |

Use `yield()` for cooperative fairness, not for polling.

```cpp
[[using gentest: test("async/yield")]]
gentest::async_test<void> yields_once() {
    co_await gentest::async::yield(); // back of the ready queue
    EXPECT_TRUE(true);
}
```

Use `manual_event` when another test or worker can make progress.

```cpp
namespace async_example {

gentest::async::manual_event server_ready;
gentest::async::manual_event client_done;
std::vector<std::string>     events;

void reset_pair_if_complete() {
    if (server_ready.is_set() && client_done.is_set()) {
        server_ready.reset();
        client_done.reset();
        events.clear();
    }
}

[[using gentest: test("async/pair/00_server")]]
gentest::async_test<void> server() {
    reset_pair_if_complete();
    events.clear();
    events.push_back("server:start");
    server_ready.set();

    co_await client_done.wait("client did not finish");

    EXPECT_EQ(events, (std::vector<std::string>{"server:start", "client:done"}));
}

[[using gentest: test("async/pair/01_client")]]
gentest::async_test<void> client() {
    reset_pair_if_complete();
    co_await server_ready.wait("server did not start");
    events.push_back("client:done");
    client_done.set();
}

} // namespace async_example
```

`manual_event::set()` wakes all current waiters and keeps the event ready for
future waiters. `reset()` only affects later waits.

Use `completion_source` for one-shot dependencies, especially when the producer
can discover that resumption is impossible.

```cpp
gentest::async::completion_source ok;
ok.complete(); // waiters resume normally

gentest::async::completion_source blocked;
blocked.fail_unresumable("connection was closed"); // waiters report BLOCKED
```

```cpp
namespace async_dependency {

std::shared_ptr<gentest::async::completion_source> reply;

[[using gentest: test("async/dependency/00_waiter")]]
gentest::async_test<void> waits_for_reply() {
    reply = std::make_shared<gentest::async::completion_source>();
    co_await reply->wait("reply did not arrive");
    EXPECT_TRUE(true);
}

[[using gentest: test("async/dependency/01_driver")]]
gentest::async_test<void> fails_dependency() {
    ASSERT_TRUE(static_cast<bool>(reply));
    reply->fail_unresumable("connection closed before reply");
    co_return;
}

} // namespace async_dependency
```

The first `complete()` or `fail_unresumable()` call wins. Later calls are ignored.

## Sync And Async Together

Gentest can run sync cases while async cases are suspended. Fixture group
boundaries are still respected: cases from a different suite/global fixture
group are not interleaved with the current group.

```cpp
namespace mixed_cases {

gentest::async::manual_event release_async;
std::vector<std::string>     order;

[[using gentest: test("mixed/00_async_waits")]]
gentest::async_test<void> async_waits() {
    if (release_async.is_set()) {
        release_async.reset();
    }
    order.clear();
    order.push_back("async:start");
    co_await release_async.wait("sync case did not release async case");
    order.push_back("async:done");
}

[[using gentest: test("mixed/01_sync_releases")]]
void sync_releases() {
    order.push_back("sync");
    release_async.set();
}

[[using gentest: test("mixed/02_check")]]
void check_order() {
    EXPECT_EQ(order, (std::vector<std::string>{"async:start", "sync", "async:done"}));
}

} // namespace mixed_cases
```

Do not depend on sleep timing for ordering. Signal with `manual_event` or
`completion_source`. If a scenario is split over multiple cases, use ordered
case names such as `00_waiter` / `01_driver`, reset sticky events for repeat
runs, and do not shuffle that scenario unless it is order-independent.

## Async Fixtures

Fixture parameters work as usual. Async setup and teardown are opt-in through
`gentest::AsyncFixtureSetup` and `gentest::AsyncFixtureTearDown`.

```cpp
struct LocalAsyncFixture : gentest::AsyncFixtureSetup, gentest::AsyncFixtureTearDown {
    int value = 0;

    gentest::async_test<void> setUp() override {
        co_await gentest::async::yield();
        value = 42;
    }

    gentest::async_test<void> tearDown() override {
        co_await gentest::async::yield();
        value = 0;
    }
};

[[using gentest: test("async/fixture/local")]]
gentest::async_test<void> uses_local_async_fixture(LocalAsyncFixture &fixture) {
    EXPECT_EQ(fixture.value, 42);
}
```

Sync and async fixtures can be used by the same async test.

```cpp
struct SyncFixture : gentest::FixtureSetup {
    int value = 0;
    void setUp() override { value = 7; }
};

[[using gentest: test("async/fixture/mixed")]]
gentest::async_test<void> mixed_fixtures(SyncFixture &sync, LocalAsyncFixture &async) {
    EXPECT_EQ(sync.value, 7);
    EXPECT_EQ(async.value, 42);
}
```

Shared fixtures can also have async lifecycle hooks.

```cpp
struct [[using gentest: fixture(suite)]] SharedAsyncFixture
    : gentest::AsyncFixtureSetup,
      gentest::AsyncFixtureTearDown {
    int value = 0;

    gentest::async_test<void> setUp() override {
        co_await gentest::async::yield();
        value = 10;
    }

    gentest::async_test<void> tearDown() override {
        co_await gentest::async::yield();
        value = 0;
    }
};

[[using gentest: test("async/fixture/shared")]]
gentest::async_test<void> uses_shared_async_fixture(SharedAsyncFixture &fixture) {
    EXPECT_EQ(fixture.value, 10);
}
```

A local fixture setup failure fails that case. A shared suite/global fixture
setup failure blocks the affected fixture group. Teardown still runs for fixture
instances whose setup completed.

## Worker Threads

The runner resumes async tests on one thread, but tests may start their own
threads. Worker threads must adopt the current context before calling
`gentest::log()`, `EXPECT_*`, or other gentest APIs.

```cpp
[[using gentest: test("async/threaded")]]
gentest::async_test<void> worker_thread_reports_back() {
    gentest::async::manual_event done;
    auto                         context = gentest::get_current_context();

    std::thread worker([context, &done] {
        auto adoption = gentest::set_current_context(context);
        gentest::log("worker reached checkpoint");
        EXPECT_TRUE(true);
        done.set();
    });

    co_await done.wait("worker did not report");
    worker.join();
}
```

Without an adopted context, test operations from a worker are a hard test
program error. Keep the `Adoption` object alive for the whole worker region that
uses gentest APIs. Prefer non-fatal `EXPECT_*` and `gentest::log()` in worker
threads. Fatal operations such as `ASSERT_*`, `gentest::fail()`, and
`gentest::skip()` throw when exceptions are enabled; do not let those exceptions
escape a `std::thread` entry point. Signal the async test and perform the fatal
operation on the runner coroutine, or catch and translate the worker exception.

## Outcomes

Async cases use the same outcomes as sync cases.

```cpp
[[using gentest: test("async/xfail")]]
gentest::async_test<void> expected_failure_after_suspend() {
    gentest::xfail("known issue");
    co_await gentest::async::yield();
    EXPECT_TRUE(false);
}

[[using gentest: test("async/skip")]]
gentest::async_test<void> skip_after_suspend() {
    co_await gentest::async::yield();
    gentest::skip("dependency not available");
}
```

There are two important non-pass async failure modes.

```cpp
[[using gentest: test("async/cannot_resume")]]
gentest::async_test<void> lost_or_never_created_resume_handle() {
    gentest::async::manual_event never_set;
    co_await never_set.wait("driver never signalled");

    // The runner has no ready work left and reports:
    // FAIL: cannot resume async test; suspended at this file:line: driver never signalled
}
```

```cpp
[[using gentest: test("async/blocked_dependency")]]
gentest::async_test<void> dependency_declares_it_cannot_resume() {
    gentest::async::completion_source source;
    source.fail_unresumable("remote peer closed before handshake");
    co_await source.wait("handshake did not finish");

    // The runner reports BLOCKED and the process exits non-zero.
}
```

`BLOCKED` is for a known impossible external dependency. A coroutine left
suspended because no resume handle exists or no one will post it is a test
failure.

Outcome notes:

- `xfail()` before an assertion or exception failure reports `XFAIL`; `xfail()`
  before a clean pass reports `XPASS`.
- `xfail()` does not make a pure blocked dependency expected. A
  `completion_source::fail_unresumable()` result remains `BLOCKED` unless the
  case has already recorded a normal failure.
- `skip()` after suspension reports `SKIP` only if the case has not already
  failed.
- With fail-fast enabled, the first non-expected failure or blocked dependency
  cancels still-pending async work in that group before later cases run.

## Live Output

On an interactive terminal, active async cases are shown as a live block below
normal log output:

```text
[  RUNNING  ] async/driver
[  YIELDED  ] async/polite
[ SUSPENDED ] async/client :: server did not start @ tests/client.cpp:27
```

Final results are printed in the normal result stream:

```text
[   PASS    ] async/server (3 ms)
[   FAIL    ] async/cannot_resume :: 1 issue(s) (3 ms)
[  BLOCKED  ] async/blocked_dependency :: remote peer closed before handshake (3 ms)
```

Non-terminal output is final-result only; it does not print the live block.

## Things To Watch

- Give every `wait()` a useful reason. It becomes the failure or live status
  text.
- Keep `manual_event` and `completion_source` objects alive longer than their
  waiters.
- Do not `co_await std::suspend_always{}` or a custom awaiter that never posts
  back to gentest's scheduler.
- Only `co_await` gentest async primitives from a gentest async test, async
  fixture, or awaited async helper running under the scheduler.
- Use `gentest::async::yield()` to let other ready async cases run. Use an event
  or completion source to wait for a condition.
- Use `gentest::log()` instead of direct `std::cout` / `std::cerr` writes while
  live progress is active.
- Adopt the current context in worker threads before using gentest APIs.
- Top-level `async_test<T>` values are discarded; only awaited helper tasks use
  their returned value.
