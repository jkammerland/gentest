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
| `gentest::async::sleep_for(...)`, `sleep_until(...)` | Suspend until a scheduler-owned steady-clock timer expires | `SUSPENDED` | The timer is canceled if the waiter is canceled |
| `event<T>::wait(key)` | Wait until `set(key, payload)` is called, then return the key's `T&` slot | `SUSPENDED` | `FAIL`: `cannot resume async test; suspended at file:line: async event key 'key' was not set` |
| `future<T>::wait(reason)` | Wait for a one-shot `promise<T>` completion, then return `T` by value | `SUSPENDED` | Depends on how the promise completes |
| `gentest::async::wait_for(...)`, `wait_until(...)` | Race a gentest-owned awaitable against a scheduler-owned timer | `SUSPENDED` | Returns `wait_result<T>` with `wait_status::timeout` |
| `promise<T>::set_blocked(reason)` | Mark an external dependency as impossible | `BLOCKED` | Fails the run and reports the reason |

Use `yield()` for cooperative fairness, not for polling.

```cpp
[[using gentest: test("async/yield")]]
gentest::async_test<void> yields_once() {
    co_await gentest::async::yield(); // back of the ready queue
    EXPECT_TRUE(true);
}
```

Use `event<T>` as a keyed, latched barrier with a stable per-key payload slot.
`wait(key)` returns the key's `T&` slot. `manual_event` is kept as an alias for
`event<std::any>`.

```cpp
[[using gentest: test("async/event/pre_set")]]
gentest::async_test<void> pre_set_typed_payload() {
    gentest::async::event<std::string> event;

    event.set("phase.loaded", "ready");
    std::string& payload = co_await event.wait("phase.loaded");

    EXPECT_EQ(payload, "ready");
}
```

`set(key, payload)` wakes all current waiters for that key and keeps the key
ready for future waiters. `set(key)` stores `T{}` when `T` is default
constructible; for `event<std::any>`, that is an empty `std::any`.
`is_set(key)` reports whether that key is currently latched ready. `reset(key)`
clears only readiness for that key; `reset_all()` clears readiness for all keys.
Resetting does not unregister already suspended waiters, invalidate the key's
payload slot, or clear its payload. Different keys do not wake each other.
`T` must be constructible from the payload passed to `set()` and
move-assignable so later `set()` calls can update the same stable slot.

```cpp
gentest::async::event<int> event;

event.set("alpha");
EXPECT_TRUE(event.is_set("alpha"));
EXPECT_FALSE(event.is_set("beta"));

int& zero = co_await event.wait("alpha");
EXPECT_EQ(zero, 0);

event.reset("alpha");
EXPECT_FALSE(event.is_set("alpha"));

event.set("loaded", 7);

int& payload = co_await event.wait("loaded");
EXPECT_EQ(payload, 7);
```

Use `manual_event` or `event<std::any>` when a key must carry different payload
types over time.

```cpp
gentest::async::manual_event event;

event.set("loaded", std::string("ok"));
std::any& payload = co_await event.wait("loaded");

EXPECT_EQ(std::any_cast<std::string&>(payload), "ok");
```

The returned payload reference is not synchronized by gentest. If another thread
or later `set(key, ...)` can mutate the same key's slot while you read or write
the payload reference, protect that interaction yourself.

`reset(key)` is allowed to discard a pulse. If `set(key)` and `reset(key)` race
a waiter between its initial readiness check and waiter registration, that waiter
may suspend until a later `set(key)`. If your protocol cannot lose a pulse,
coordinate `set`, `reset`, and `wait` with an external primitive appropriate for
the flow, such as an atomic state machine, mutex, channel, or fixture-owned
sequencer.

Use `promise<T>` and `future<T>` for one-shot dependencies, especially when a
worker thread, callback, or third-party event loop needs to resume a gentest
coroutine. The API follows the STL shape: create a promise, call `get_future()`,
complete the promise from the producer, and `co_await` the future from the test.

```cpp
[[using gentest: test("async/promise/worker")]]
gentest::async_test<void> worker_completion() {
    gentest::async::promise<std::string> promise;
    auto future = promise.get_future();

    std::thread worker([p = std::move(promise)]() mutable {
        p.set_value("ok");
    });

    std::string value = co_await future.wait("worker did not finish");
    worker.join();
    EXPECT_EQ(value, "ok");
}

[[using gentest: test("async/promise/blocked")]]
gentest::async_test<void> blocked_dependency() {
    gentest::async::promise<void> promise;
    auto future = promise.get_future();

    promise.set_blocked("connection was closed");
    co_await future.wait("connection handshake");
}
```

`set_blocked(reason)` means "stop waiting; this dependency can never produce a
successful value." Any current or future waiter resumes and the test is reported
as `BLOCKED` with that reason. Use it for missing prerequisites or permanent
external conditions, not for product bugs.

```cpp
p.set_value(response);            // waiter returns response
p.set_exception(error);           // waiter throws; test FAILS
p.set_blocked("not available");   // waiter reports BLOCKED
```

This is different from a timeout. A timeout says "not ready yet within this
duration." `set_blocked(...)` says "it will not become ready."

The first terminal state wins. Use `set_value(...)`, `set_exception(...)`, or
`set_blocked(...)` when duplicate completion is a bug. Use `try_set_value(...)`,
`try_set_exception(...)`, or `try_set_blocked(...)` when callbacks can race and
late completions should be ignored.

```cpp
gentest::async::promise<Response> promise;
auto future = promise.get_future();

client.async_read([p = std::move(promise)](Result<Response> result) mutable {
    if (result.ok()) {
        (void)p.try_set_value(std::move(result.value()));
    } else if (result.closed()) {
        (void)p.try_set_blocked("client closed before response");
    } else {
        (void)p.try_set_exception(
            std::make_exception_ptr(std::runtime_error(result.message())));
    }
});

Response response = co_await future.wait("client response");
```

`future<T>` is move-only and single-consumer. It returns `T` by value, so use
move-only payloads when ownership should transfer into the coroutine.

## Timers And Timeouts

`sleep_for(duration)` and `sleep_until(deadline)` use the gentest scheduler;
they do not start a timer thread. Other ready async cases in the same fixture
group continue while the sleeper is pending.

Use `wait_for(awaitable, duration)` or `wait_until(awaitable, deadline)` when a
test owns a timeout. Supported awaitables are gentest-owned primitives:
`event<T>::wait(...)`, `future<T>::wait(...)`, `sleep_for`/`sleep_until`,
`yield()`, and `async_test<T>` helper tasks. Custom awaiters are rejected at
compile time.

```cpp
[[using gentest: test("async/timeout")]]
gentest::async_test<void> explicit_timeout() {
    gentest::async::manual_event ready;

    auto result = co_await gentest::async::wait_for(
        ready.wait("external ready"),
        std::chrono::milliseconds(50));

    if (result.timed_out()) {
        gentest::skip("external service did not become ready");
    }
}
```

For `event<T>::wait(key)`, a ready result contains the stable key slot by
reference. A timeout contains no value and cancels only that waiter. The event
itself keeps working; a later `set(key, value)` stores the payload for future
waiters.

```cpp
[[using gentest: test("async/event_timeout")]]
gentest::async_test<void> event_timeout() {
    gentest::async::event<int> event;

    auto result = co_await gentest::async::wait_for(
        event.wait("loaded"),
        std::chrono::milliseconds(10));

    if (result.timed_out()) {
        event.set("loaded", 42); // stored for a later waiter
        co_return;
    }

    int& payload = result.value();
    EXPECT_EQ(payload, 42);
}
```

For `future<T>::wait(reason)`, a ready result contains the future value by
value. A timeout contains no value and cancels that waiter. `future<T>` remains
single-consumer for active waits and final value consumption: a timed-out or
dropped wait does not consume the future, so a later wait can still receive the
value, exception, or blocked result.

```cpp
[[using gentest: test("async/future_timeout")]]
gentest::async_test<void> future_timeout() {
    gentest::async::promise<std::string> promise;
    auto future = promise.get_future();

    auto result = co_await gentest::async::wait_for(
        future.wait("reply"),
        std::chrono::milliseconds(10));

    if (result.timed_out()) {
        (void)promise.try_set_value("late"); // ignored by this timed-out wait
        co_return;
    }

    std::string reply = std::move(result).value();
    EXPECT_EQ(reply, "ok");
}
```

`wait_result<T>::ready()`, `timed_out()`, `status()`, and `operator bool`
report the outcome. `value()` returns the ready value, returns `T&` for
reference awaitables such as `event<T>::wait(...)`, and moves out of an rvalue
`wait_result<T>`. Calling `value()` after a timeout is a logic error.

Default waits are intentionally unbounded. If a test leaks a
`CurrentContextLease`, or waits forever without an explicit `wait_for` /
`wait_until`, the run may hang by contract. Gentest does not add a hidden
cleanup timeout.

## Scheduler Ordering

The async runner is cooperative. It never preempts a running coroutine or sync
case. A coroutine runs until it returns, throws, or reaches a `co_await` that
suspends.

Ready work is kept in a FIFO queue. Starting an async case, `yield()`, an event
set, a promise completion, and an expired timer all post work to that queue.
`yield()` posts the current coroutine at the back of the queue, so other already
ready cases run first.

Timers are checked at scheduler checkpoints: before ready work is pumped and
when the scheduler is otherwise waiting for progress. When a timer expires, its
waiter is posted to the ready queue; it does not interrupt the case currently
running. If ready work is already queued, that queued work keeps its FIFO order
and the expired timer resumes after it. Multiple timers found due in the same
scan are posted together; do not depend on a stable order for equal or nearly
equal deadlines.

Unready coroutines do not execute. They stay suspended until their primitive
posts a resume token:

- `event<T>::wait(key)` resumes when `set(key, ...)` posts the key's waiters.
- `future<T>::wait(...)` resumes when the promise reaches a terminal state.
- `sleep_for` / `sleep_until` resumes when the scheduler observes the deadline.
- `wait_for` resumes when either the wrapped awaitable wins before the deadline
  or the timeout wins. If the wrapped awaitable is already ready at the await
  site, it wins even when the deadline has already expired. Once a suspended
  timed wait observes an expired deadline, a later event, future, or child
  completion returns `wait_status::timeout`. The loser is canceled.

While async cases are suspended on timers or events, sync cases in the same
fixture group may still run. At the end of a fixture group, the runner drains
remaining ready work and waits for pending scheduler timers or adopted work
before deciding whether suspended async cases are unresumable.

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
    co_return;
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
    co_return;
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
    co_return;
}
```

A local fixture setup failure fails that case. A shared suite/global fixture
setup failure blocks the affected fixture group. Teardown still runs for fixture
instances whose setup completed.

Every function returning `gentest::async_test<T>` must be a coroutine. If the
body does not need to suspend, end it with `co_return;`.

## Worker Threads

The runner resumes async tests on one thread, but tests may start their own
threads. Worker threads may lease the current context for `gentest::log()` and
`context.stop_token()` only. They must report results back to the owning test,
and the owning test performs `EXPECT_*`, `ASSERT_*`, `gentest::fail()`,
`gentest::skip()`, `gentest::xfail()`, and mock configuration.

```cpp
[[using gentest: test("async/threaded")]]
gentest::async_test<void> worker_thread_reports_back() {
    gentest::async::event<std::string> done;
    auto                               context = gentest::get_current_context();

    std::thread worker([context, &done] {
        auto lease = gentest::set_current_context(context);
        gentest::log("worker reached checkpoint");
        done.set("worker.done", "ok");
    });

    std::string& payload = co_await done.wait("worker.done");
    worker.join();
    EXPECT_EQ(payload, "ok");
}
```

Use the context stop token for cooperative worker shutdown. This works directly
with `std::condition_variable_any`.

```cpp
[[using gentest: test("async/threaded_stop")]]
gentest::async_test<void> worker_thread_stops() {
    std::mutex                  m;
    std::condition_variable_any cv;
    bool                        ready = false;
    bool                        worker_saw_ready = false;
    std::latch                  started(1);

    auto context = gentest::get_current_context();

    std::thread worker([context, &m, &cv, &ready, &worker_saw_ready, &started] {
        auto lease = gentest::set_current_context(context);
        auto stop  = context.stop_token();
        started.count_down();

        std::unique_lock lk(m);
        const bool completed = cv.wait(lk, stop, [&] { return ready; });
        if (!completed) {
            gentest::log("worker stopped");
            return;
        }

        worker_saw_ready = ready;
    });

    started.wait();
    {
        std::lock_guard lk(m);
        ready = true;
    }
    cv.notify_all();
    worker.join();
    EXPECT_TRUE(worker_saw_ready);
    co_return;
}
```

For plain `std::condition_variable`, bridge the stop token with a callback.
The callback should notify the primitive only; if it needs to log, explicitly
lease the context inside the callback. Do not assert from stop callbacks.

```cpp
std::stop_callback wake_on_stop(context.stop_token(), [&] {
    cv.notify_all();
});

cv.wait(lock, [&] {
    return ready || context.stop_requested();
});
```

Without a leased context, `gentest::log()` from a worker is a hard test program
error. With a leased context, outcome-changing APIs from a worker are still a
hard test program error. Signal the owning async test and perform assertions or
outcome changes there.

## Logs

`gentest::log()` is captured on the current test and streamed to the registered
log sinks immediately. The default sink writes to stdout. Add more sinks when a
consumer needs a copy, and remove them explicitly with the returned handle.
Sink handles are explicit removal tokens, not RAII guards; destroying a handle
does not unregister the sink. Configure sinks outside active logging, and do not
change the sink registry concurrently with `gentest::log()`.

```cpp
std::ostringstream log_copy;
auto handle = gentest::add_log_sink(gentest::make_ostream_log_sink(log_copy));

gentest::log("visible on stdout and copied");

handle.remove();
```

While async live progress is active, stdout logs from active async cases are
shown in that case's live row tail, then removed when the case completes; the
completed row keeps the log count. Additional custom sinks still receive each
log immediately. Active async rows show the last five log lines by default.
Tune this with:

```text
--async-log-tail=0  # count only
--async-log-tail=10 # keep ten recent lines per active async case
```

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
    // async event key 'external.signal' was not set
}
```

```cpp
[[using gentest: test("async/blocked_dependency")]]
gentest::async_test<void> dependency_declares_it_cannot_resume() {
    gentest::async::promise<void> promise;
    auto future = promise.get_future();

    promise.set_blocked("remote peer closed before handshake");
    co_await future.wait("handshake did not finish");

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
  `promise<T>::set_blocked()` result remains `BLOCKED` unless the case has
  already recorded a normal failure.
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
[ SUSPENDED ] async/waiting :: async event key 'external.signal' was not set @ tests/waiting.cpp:27
```

Final results are printed in the normal result stream:

```text
[   PASS    ] async/worker (3 ms)
[   FAIL    ] async/cannot_resume :: 1 issue(s) (3 ms)
[  BLOCKED  ] async/blocked_dependency :: remote peer closed before handshake (3 ms)
```

Non-terminal output is final-result only; it does not print the live block.

## Things To Watch

- Use stable, descriptive `event<T>` keys. The key becomes the failure or
  live status text when it cannot resume.
- An `event<T>` payload is a stable key slot, not a per-wait snapshot. The
  returned `T&` remains valid only while the event object lives, and
  payload access is user-synchronized.
- Keep `event<T>` objects alive longer than their waiters. Destroying an
  incomplete `promise<T>` marks its future as `BLOCKED` with a broken-promise
  reason.
- Do not `co_await std::suspend_always{}` or a custom awaiter that never posts
  back to gentest's scheduler.
- Only `co_await` gentest async primitives from a gentest async test, async
  fixture, or awaited async helper running under the scheduler.
- Use `gentest::async::yield()` to let other ready async cases run. Use an
  event for keyed/broadcast coordination, or a promise/future pair for one-shot
  external completion.
- Use `gentest::log()` instead of direct `std::cout` / `std::cerr` writes while
  live progress is active.
- Use `--async-log-tail=N` to tune the number of recent per-case log lines shown
  in the live async status block.
- Lease the current context in worker threads only for `gentest::log()` and
  cooperative stop observation.
- Do not assert, skip, xfail, fail, or configure mocks from worker threads or
  stop callbacks. Signal the owning test and assert there.
- Top-level `async_test<T>` values are discarded; only awaited helper tasks use
  their returned value.
