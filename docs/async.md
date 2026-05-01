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
| `manual_event::wait(key)` | Wait until `set(key, payload)` is called, then return the key's `std::any&` slot | `SUSPENDED` | `FAIL`: `cannot resume async test; suspended at file:line: manual event key 'key' was not set` |
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

Use `manual_event` as a keyed, latched barrier with a stable per-key payload
slot. `wait(key)` returns the key's `std::any&` slot.

```cpp
[[using gentest: test("async/manual_event/pre_set")]]
gentest::async_test<void> pre_set_payload() {
    gentest::async::manual_event event;

    event.set("phase.loaded", std::string("ready"));
    std::any& payload = co_await event.wait("phase.loaded");

    EXPECT_EQ(std::any_cast<std::string&>(payload), "ready");
}
```

`set(key, payload)` wakes all current waiters for that key and keeps the key
ready for future waiters. `set(key)` stores an empty `std::any`. `is_set(key)`
reports whether that key is currently latched ready. `reset(key)` clears only
readiness for that key; `reset_all()` clears readiness for all keys. Resetting
does not invalidate the key's `std::any&` slot or clear its payload. Different
keys do not wake each other.

```cpp
gentest::async::manual_event event;

event.set("alpha");
EXPECT_TRUE(event.is_set("alpha"));
EXPECT_FALSE(event.is_set("beta"));

std::any& signal_only = co_await event.wait("alpha");
EXPECT_FALSE(signal_only.has_value());

event.reset("alpha");
EXPECT_FALSE(event.is_set("alpha"));

event.set("loaded", std::string("ok"));

std::any& payload = co_await event.wait("loaded");
ASSERT_TRUE(payload.has_value());
EXPECT_EQ(std::any_cast<std::string&>(payload), "ok");
```

The returned payload reference is not synchronized by gentest. If another thread
or later `set(key, ...)` can mutate the same key's slot while you read or write
the `std::any&`, protect that interaction yourself.

`reset(key)` is allowed to discard a pulse. If `set(key)` and `reset(key)` race
a waiter between its initial readiness check and waiter registration, that waiter
may suspend until a later `set(key)`. If your protocol cannot lose a pulse,
coordinate `set`, `reset`, and `wait` with an external primitive appropriate for
the flow, such as an atomic state machine, mutex, channel, or fixture-owned
sequencer.

Use `completion_source` for one-shot dependencies, especially when the producer
can discover that resumption is impossible.

```cpp
[[using gentest: test("async/completion_source/worker")]]
gentest::async_test<void> worker_completion() {
    auto ok = std::make_shared<gentest::async::completion_source>();

    std::thread worker([ok] { ok->complete(); });
    co_await ok->wait("worker did not finish");
    worker.join();
}

[[using gentest: test("async/completion_source/blocked")]]
gentest::async_test<void> blocked_dependency() {
    gentest::async::completion_source blocked;
    blocked.fail_unresumable("connection was closed");
    co_await blocked.wait("connection handshake");
}
```

The first `complete()` or `fail_unresumable()` call wins. Later calls are ignored.

## Sync And Async Together

Gentest can run sync cases while async cases are suspended. Fixture group
boundaries are still respected: cases from a different suite/global fixture
group are not interleaved with the current group.

```cpp
[[using gentest: test("mixed/sync")]]
void sync_case() {
    EXPECT_TRUE(true);
}

[[using gentest: test("mixed/async")]]
gentest::async_test<void> async_case() {
    co_await gentest::async::yield();
    EXPECT_TRUE(true);
}
```

Do not coordinate async behavior by depending on test execution order. Ordered
cross-case async choreography is an anti-pattern; use keyed barriers, worker
signals, fixtures, or a single async case that owns the flow.

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
        done.set("worker.done", std::string("ok"));
    });

    std::any& payload = co_await done.wait("worker.done");
    worker.join();
    EXPECT_EQ(std::any_cast<std::string&>(payload), "ok");
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
    co_await never_set.wait("external.signal");

    // The runner has no ready work left and reports:
    // FAIL: cannot resume async test; suspended at this file:line:
    // manual event key 'external.signal' was not set
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
[  RUNNING  ] async/worker
[  YIELDED  ] async/polite
[ SUSPENDED ] async/waiting :: manual event key 'external.signal' was not set @ tests/waiting.cpp:27
```

Final results are printed in the normal result stream:

```text
[   PASS    ] async/worker (3 ms)
[   FAIL    ] async/cannot_resume :: 1 issue(s) (3 ms)
[  BLOCKED  ] async/blocked_dependency :: remote peer closed before handshake (3 ms)
```

Non-terminal output is final-result only; it does not print the live block.

## Things To Watch

- Use stable, descriptive `manual_event` keys. The key becomes the failure or
  live status text when it cannot resume.
- A `manual_event` payload is a stable key slot, not a per-wait snapshot. The
  returned `std::any&` remains valid only while the event object lives, and
  payload access is user-synchronized.
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
