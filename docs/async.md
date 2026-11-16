# Async tests

An async test is a coroutine. It can `co_await` timers, events and your own
asynchronous operations instead of blocking a thread, and gentest runs other
ready cases while it waits. Async tests are experimental.

```cpp
#include "gentest/async.h"
#include "gentest/test.h"

namespace async_examples::cases {

using namespace gentest;
using namespace gentest::async;

inline auto answer() -> awaitable<int> {
    co_await yield();
    co_return 42;
}

[[gentest::test("yield_and_return")]]
inline auto yieldAndReturn() -> awaitable<> {
    const int value = co_await answer();
    expect_eq(value, 42);
}

} // namespace async_examples::cases
```

A case is async when it returns `gentest::awaitable<>`. Include
`gentest/async.h` (or use `import gentest;`) and build the test target as
usual: `gentest::gentest_main` runs async cases, and fixtures own any event loop
or service that cases share. The async API needs no other coroutine library.

The example is stage 1 of the [async examples](../examples/async/), one
runnable project in eight stages; each section below follows one stage.

Only test cases can be async. Codegen rejects a bench or jitter case that
returns `awaitable<T>` with
`gentest::awaitable<T> return types are supported only for test cases, not bench or jitter cases`.

## 1. Awaitables and yield

[01_awaitables.hpp](../examples/async/01_awaitables.hpp) is the example above.

`awaitable<>` means `awaitable<void>`; a helper can return `awaitable<T>`. A
coroutine starts when it is awaited, and awaiting a helper returns its value or
rethrows its exception. A case's own return value is discarded, so a case
(including a parameterized or template case) can return any `awaitable<T>`.

An awaitable has one waiter at a time. Await a gentest `awaitable` only from the
case's own coroutines (the case and the helpers it awaits). Awaiting it from two
coroutines at once, or from another coroutine type, throws `std::logic_error`.
To wait for another library's operations, wrap them in an ordinary awaiter
(stage 7).

`yield()` queues the current coroutine behind other ready work. `sleep_for()`
and `sleep_until()` suspend until a scheduler timer expires; neither needs an
event loop of your own. Time points use `std::chrono::steady_clock`.

## 2. Collect results or race operations

[02_composition.hpp](../examples/async/02_composition.hpp) runs independent
coroutines together. With `answer()` from stage 1:

```cpp
auto [left, right] = co_await when_all(answer(), answer());
expect_eq(left + right, 84);
```

`when_all()` starts its operations and waits for every one to finish. The result
is an owning `std::tuple` in input order, even if completion order differs. A
void operation contributes `std::monostate`. If an operation throws, the others
run to completion before the first exception is rethrown.

`when_any()` selects the first value or exception, requests stop for the other
operations, and waits for all of them to finish. It then returns an owning
`std::variant` whose index identifies the winning input, or rethrows the winning
exception. A void result contributes `std::monostate`. Losing operations never
continue detached after the call returns.

Call both as shown, without naming template arguments or taking their address:
they are class templates that record the caller's source location for
suspension events and failure messages, as `wait_for()` and `wait_until()` do.

Both accept any C++ awaitable, including member and free `operator co_await`.
Lvalue inputs are borrowed, so keep them alive through the await; rvalue inputs
are owned. Result references are copied into the owning tuple or variant, so
their values must be copyable; move-only values returned by value stay
move-only.

## 3. Cancellation and deadlines

[03_cancellation.hpp](../examples/async/03_cancellation.hpp) shows bounded waits
and an explicit stop token. Use `wait_for(operation, duration)` or
`wait_until(operation, deadline)` when the case owns a deadline:

```cpp
using namespace std::chrono_literals;

event<int> replies;
replies.set("request-1", 42);
auto result = co_await wait_for(replies.wait("request-1"), 1s);
if (result.timed_out()) {
    fail("request-1 did not receive a reply");
    co_return;
}
expect_eq(result.value(), 42);
```

A timeout is a returned status; the case chooses whether to fail, skip or
continue. `wait_result<T>` provides `ready()`, `timed_out()`, `status()` and a
boolean conversion. Call `value()` only when the result is ready; after a timeout
it throws `std::logic_error`. `value()` preserves references from operations
such as event waits (keep their source alive while you use them), and
`std::move(result).value()` extracts an owned value. A void operation returns
`wait_result<void>`.

Timed waits accept any awaitable. On timeout, gentest requests stop and joins
the operation before it returns, which can take longer than the requested
duration if a backend is slow to stop or ignores cancellation. A deadline can't
preempt running code, and an already-ready value wins even after the deadline.

Cancellation flows through nested coroutines and compositions. Event, future,
sleep and yield waits inherit that stop request and throw `operation_cancelled`
when cancelled. An explicit `std::stop_token` overrides the inherited one:

```cpp
std::stop_source stop;
stop.request_stop();
try {
    (void)co_await replies.wait("missing", stop.get_token());
} catch (const operation_cancelled &) {
    // Release application resources here.
}
co_await yield(std::stop_token{}); // Cleanup may need an uncancelled wait.
```

The token overloads are `event.wait(name, token)`, `future.wait(token)`,
`sleep_for(duration, token)`, `sleep_until(deadline, token)` and `yield(token)`.
Use an explicit empty token only for cleanup that must finish after a stop. A
timeout or cancellation retires only that wait; a shared event, future or
executor stays usable.

`when_any()`, `wait_for()`, `wait_until()` and the stop-token overloads need
exceptions, and using them in a build without exceptions is a compile-time
error. Such builds can use `awaitable<T>`, `when_all()` and the waits without a
token; there is no separate cancellation-status API.

## 4. Fixtures own shared infrastructure

[04_fixtures.hpp](../examples/async/04_fixtures.hpp) shows a local connection
with async setup and teardown, and a shared server with async setup.

Implement `gentest::AsyncFixtureSetup::setUp()`,
`gentest::AsyncFixtureTearDown::tearDown()` or both, returning `awaitable<>`.
A fixture without an attribute is local to a case.
`[[gentest::fixture(suite)]]` shares a fixture with the cases in its namespace
and its child namespaces, and `[[gentest::fixture(global)]]` keeps it until the
end of the run. [Fixtures](fixtures.md) covers lifetimes and scoping.

Two rules apply to async cases:

- An async case can take at most one suite or global fixture parameter. Codegen
  rejects more: `async tests may depend on at most one suite/global fixture parameter; multiple shared fixture parameters would make execution order parameter-dependent`.
- A local fixture's async `setUp()` and `tearDown()` run only for async cases.
  A synchronous case that takes the same fixture doesn't call them; give such a
  fixture `gentest::FixtureSetup` or `gentest::FixtureTearDown` instead.

Use these fixtures to own an Asio `io_context`, an executor or another shared
service, and shut down backend work and join owned threads before the fixture is
destroyed. Stages 6 and 7 show this.

## 5. Named typed events and one-shot futures

[05_events.hpp](../examples/async/05_events.hpp) puts an `event<Connected>`
channel in a suite fixture. Cases await `wait("primary")` and receive a
`Connected&`. Each case observes fixture state without depending on another case
running first.

| Operation | Meaning |
| --- | --- |
| `event<T>::set(name, value)` | Store a value and wake every current observer of that name |
| `event<T>::wait(name)` | Observe the stored `T&`, waiting if necessary |
| `event<T>::is_set(name)` | Check whether the name is ready |
| `event<T>::reset(name)` / `reset_all()` | Clear readiness, keeping payload slots |
| `promise<T>::get_future()` | Obtain a single-consumer, one-shot result |
| `promise<T>::set_value(value)` | Complete that result, including with a move-only value |

An event is latched: waiting doesn't consume the value, so a later waiter sees
it too. `set(name)` stores `T{}` when `T` supports it, and `manual_event` is
`event<std::any>`. Reset doesn't take back resumes already queued or destroy the
payload. The slot keeps a stable address, but another `set()` can replace its
contents, so synchronize access when writers and readers can overlap.

A future returns its value by value and supports one active wait and one
consumption; a timed-out or dropped wait doesn't consume it. `set_exception()`
propagates an exception. `set_blocked(reason)`, or dropping an unfinished
promise, ends the waiting case as blocked (see [Reports](reports.md)).
`try_set_value()`, `try_set_exception()` and `try_set_blocked()` return whether
they completed the promise first.

## 6. Drive a user loop

[06_polling.hpp](../examples/async/06_polling.hpp) contains a complete small
loop owned by a suite fixture. The case calls `pollOne()` itself and uses
`yield()` to give other ready cases a turn. In your code, call
`io_context::poll_one()`, an engine tick or another executor's operation.

A callback that resumes an awaiter may run while a different case is polling the
loop. Use a guarded continuation (stage 7) for that handoff, so gentest restores
the owning case's context. Calling a raw coroutine handle from the loop doesn't
restore it.

## 7. Adapt a worker callback with an ordinary awaiter

[07_worker.hpp](../examples/async/07_worker.hpp) contains a complete worker loop
and a `Read` awaiter. Its fixture drains queued work and joins the worker when it
is destroyed. The awaiter uses only `await_ready()`, `await_suspend(handle)` and
`await_resume()`. The same `Read` works directly and inside `wait_for()`.

The callback handoff has four steps:

1. In `await_suspend()`, obtain a guarded continuation with
   `handle.promise().make_continuation()` or `make_continuation(handle, reason)`.
2. Bind a `noexcept` result receiver with
   `bind_continuation<Args...>(next, receiver)`. The receiver stores the result
   for `await_resume()`.
3. Give the bound callback to the backend, which can call it on any thread,
   even inline during registration. The runner takes the arguments and calls the
   receiver on its own thread with the case's context.
4. Return a value or throw an application exception from `await_resume()`.
   Make assertions in the resumed case.

The worker example passes a status and a `std::unique_ptr<int>` through this
handoff, and passes `next.stop_token()` to the worker so that a stop produces a
cancelled completion instead of a dropped callback. The handoff doesn't need an
[adopted context](logging_and_threads.md).

The backend stores the callback however it likes (for example as
`std::function<bool(Reply)>` or `std::function<void()>`) and keeps its own
queue, synchronization and shutdown policy. A bound callback returns whether it
was queued; a `void` wrapper may discard that value.

## 8. Acknowledge stop, then release the registration

[08_callback_cancellation.hpp](../examples/async/08_callback_cancellation.hpp)
models a read that never gets a reply. It registers a `std::stop_callback` on
the continuation's token, so a stop request makes the backend complete the read
and the resumed awaiter throws `operation_cancelled`. The awaiter's destructor
then removes the registration before `when_any()` returns. With an expired
deadline, `wait_for()` times out without registering the read at all.

Cancellation must let the operation finish: clearing the callback alone leaves
its coroutine suspended, so a composition can't join it. Cancel one operation;
don't stop an executor shared with other cases.

A threaded backend must also synchronize callback storage and keep buffers alive
until its I/O has stopped. A late guarded callback can safely submit owned
arguments but can't resume an abandoned coroutine; rejected arguments may be
destroyed on the producer thread.

## Scheduling and output

Ready continuations resume in FIFO order. Timers are checked before ready work
is resumed and while the runner waits for progress; they can't preempt a running
case. Each fixture group finishes all its async cases before the next group
starts, so don't make a case depend on a later group.

A case that is suspended with no ready work, timers, live continuations or
adopted work can never resume. It fails with `cannot resume async test`,
followed by where it is suspended and what it waits for. For example, an event
that nothing will set fails this way. Use `promise::set_blocked()` when the case
should end as blocked instead.

Assertions, failures, skips and mock configuration belong to the case's own
coroutines. A worker thread needs an [adopted context](logging_and_threads.md)
only to log, to observe the case's stop or to publish progress with
`gentest::currentNotifier()`; guarded continuations need none.

In a terminal, runs with async cases show a live view; see
[Live output for async runs](logging_and_threads.md#live-output-for-async-runs).
When a progress watchdog is configured, each suspension of an async case can
renew the case's progress (see [Notifications](notifications.md)); the deadlines
on this page are independent of it.

The async API is in `gentest/async.h`, which `gentest/runner.h` includes, and in
`import gentest;`.

## Advanced: continuation lifetime

A `continuation_handle` represents one suspended operation. Each
`make_continuation()` call invalidates earlier handles. Copy one handle when
several callbacks compete to complete the same operation; at most one
completion is queued. The returned boolean means queued, not delivered or
successful.

The awaiter owns its handle. If `await_suspend()` returns `false`, or
registration throws, cancel the handle first (and release any backend resources
before rethrowing). Keep a local copy until registration succeeds, as the worker
example does. Awaiters that return a coroutine handle from `await_suspend()`
need no gentest-specific hook.

`continuation_handle::cancel()` retires that handle and its copies. It doesn't
cancel the backend or resume the coroutine, so backend cancellation needs its
own completion path. Cancelling after a successful `try_invoke()` also drops a
completion the runner hasn't delivered yet, so don't cancel a handle whose
completion the coroutine needs.

Callback arguments, including reference parameters, are copied or moved into
owned storage; `std::reference_wrapper` borrows its referent, which must outlive
delivery. Binding and registration may allocate and throw. An exception during
callback dispatch, including queue allocation, terminates the process.

A live guarded continuation keeps gentest waiting for external work; completing,
cancelling or dropping the last handle wakes the runner. A promise or event
alone doesn't: keep a continuation alive while external work runs, or bound the
wait with a deadline.

## Advanced: an application-owned executor

A fixture can borrow an executor that already belongs to the application. Give
the fixture a static setter:

```cpp
struct ApplicationExecutor; // The executor type your application already has.

namespace app {

struct [[gentest::fixture(global)]] Infrastructure {
    static void setExecutor(ApplicationExecutor &value) { executor = &value; }
    inline static ApplicationExecutor *executor = nullptr;
};

} // namespace app
```

Call the setter from your own `main()` before `run_all_tests()`, and keep the
executor alive until that call returns:

```cpp
// main.cpp: also include the headers that define ApplicationExecutor and app::Infrastructure.
#include "gentest/runner.h"

int main(int argc, char **argv) {
    ApplicationExecutor executor;
    app::Infrastructure::setExecutor(executor);
    return gentest::run_all_tests(argc, argv);
}
```

Cases in `app` and its child namespaces receive `app::Infrastructure&` as a
fixture parameter. Because you provide `main()`, link `gentest::gentest`
instead of `gentest::gentest_main`:

```cmake
target_link_libraries(my_tests PRIVATE gentest::gentest gentest::gentest_runtime)
```
