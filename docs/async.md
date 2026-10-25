# Async tests

An async case returns `gentest::awaitable<>`; a helper can return
`gentest::awaitable<T>`. Gentest runs and resumes these coroutines on its runner
thread. Other ready cases in the same fixture group can run while a case is
suspended.

Start with the runnable [async examples](../examples/async/). The eight stages
cover coroutines, composition, cancellation, fixtures, events, and complete user
backends. Each case works independently and with repeat and shuffle enabled.
The normal `gentest::gentest_main` entry point is sufficient. Fixtures own the
infrastructure; no event-loop registration or application runner is required.

## 1. Awaitables and yield

[01_awaitables.hpp](../examples/async/01_awaitables.hpp) starts with a helper that
returns a value:

```cpp
#include "gentest/async.h"
#include "gentest/test.h"

using namespace gentest;
using namespace gentest::async;

inline auto answer() -> awaitable<int> {
    co_await yield();
    co_return 42;
}

[[gentest::test("uses_answer")]]
inline auto usesAnswer() -> awaitable<> {
    const int value = co_await answer();
    expect_eq(value, 42);
}
```

`awaitable<>` means `awaitable<void>`. Its coroutine starts when awaited. Awaiting
a helper propagates its value or exception. Top-level return values are discarded.
Parameterized and template cases use the same return type. One awaitable object
supports one active waiter; concurrent waits on the same object are rejected.

`yield()` queues the current coroutine behind other ready work. `sleep_for()`
and `sleep_until()` suspend until a scheduler timer expires. Neither needs a
user event loop. Time points use `std::chrono::steady_clock`.

## 2. Collect results or race operations

[02_composition.hpp](../examples/async/02_composition.hpp) runs independent
coroutines together:

```cpp
auto [left, right] = co_await when_all(answer(), answer());
expect_eq(left + right, 84);
```

`when_all()` starts its operations and waits for every one to finish. The result
is an owning `std::tuple` in input order, even if completion order differs. A
void operation contributes `std::monostate`. If an operation throws, the others
still finish before the first exception is rethrown.

`when_any()` selects the first value or exception, requests stop for the other
operations, and waits for all of them to finish. It then returns an owning
`std::variant`, whose index identifies the winning input, or rethrows the winning
exception. A void result contributes `std::monostate`. Losing operations never
continue detached after the call returns.

Both functions accept ordinary C++ awaitables, including member and free
`operator co_await`. Lvalue inputs are borrowed; keep them alive through the
await. Rvalue inputs are owned. Result references are copied into the owning
tuple or variant, so their referred-to values must be copyable. Move-only values
returned by value remain move-only.

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

A timeout is a returned status; the case chooses whether to fail, skip, or
continue. `wait_result<T>` provides `ready()`, `timed_out()`, `status()`, and a
boolean conversion. Call `value()` only when ready. It preserves references
from operations such as event waits; `std::move(result).value()` extracts an
owned value. A void operation returns `wait_result<void>`.

A reference result borrows its source. Keep that source alive while using the
reference; a timed wait does not extend the lifetime of a temporary's members.

Timed waits accept ordinary awaitables without an extra subscription method.
On timeout, Gentest requests stop and joins the operation before returning the
timeout result. This can take longer than the requested duration when a backend
needs time to stop or ignores cancellation. A deadline cannot preempt running
code. An already-ready value wins even when the deadline has expired.

Cancellation flows through nested coroutines and compositions. Event, future,
sleep, and yield waits inherit that stop request and throw `operation_cancelled`
when cancelled. An explicit `std::stop_token` overrides the inherited token:

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
`sleep_for(duration, token)`, `sleep_until(deadline, token)`, and `yield(token)`.
An explicit empty token permits cleanup to finish after a coroutine has been stopped;
use it only for work that must complete. A timeout or cancellation retires that
wait, leaving a shared event, future, or executor available to other operations.

`when_any()`, `wait_for()`, and `wait_until()` require exceptions for cooperative
cancellation and joining. Exception-disabled builds can still use `awaitable<T>` and
`when_all()`; no additional cancellation-status API is provided.

## 4. Fixtures own shared infrastructure

[04_fixtures.hpp](../examples/async/04_fixtures.hpp) shows a local connection with
async setup/teardown and a shared server with async setup.

Implement `gentest::AsyncFixtureSetup::setUp()` and/or
`gentest::AsyncFixtureTearDown::tearDown()`, returning `awaitable<>`. An unannotated
fixture is local to a case. `[[gentest::fixture(suite)]]` shares a fixture with
consumers in its namespace and descendants; `[[gentest::fixture(global)]]`
gives it run lifetime.

Use these fixtures to own an Asio `io_context`, an executor, or another shared
service. Shut down backend work and join owned worker threads before destroying
the infrastructure. The later stages show a complete polled loop and a complete
worker loop with this ownership model.

## 5. Named typed events and one-shot futures

[05_events.hpp](../examples/async/05_events.hpp) puts an `event<Connected>` channel
in a suite fixture. Cases await `wait("primary")` and receive a `Connected&`.
Each case observes fixture state without depending on another case running first.

| Operation | Meaning |
| --- | --- |
| `event<T>::set(name, value)` | Store a value and wake every current observer of that name |
| `event<T>::wait(name)` | Observe the stored `T&`, waiting if necessary |
| `event<T>::is_set(name)` | Check whether the name is ready |
| `event<T>::reset(name)` / `reset_all()` | Clear readiness, retaining payload slots |
| `promise<T>::get_future()` | Obtain a single-consumer, one-shot result |
| `promise<T>::set_value(value)` | Complete that result, including a move-only value |

An event is latched: a later waiter sees the value too. Waiting does not consume
it. `set(name)` stores `T{}` when supported. `manual_event` aliases
`event<std::any>`. Reset clears readiness; it does not retract already queued
resumes or destroy the payload. The slot stays at a stable address, but another
`set()` can replace its contents. Synchronize application access to a shared
payload when writers and readers can overlap.

A future returns its value by value. Only one active wait and one consumption
are supported. A timed-out or dropped wait does not consume the result.
`set_exception()` propagates an exception; `set_blocked(reason)` reports an
impossible dependency as `BLOCKED`. Dropping an unfinished promise also produces
a blocked result. `try_set_value()`, `try_set_exception()`, and
`try_set_blocked()` report whether they completed it first.

## 6. Drive a user loop

[06_polling.hpp](../examples/async/06_polling.hpp) contains a complete small loop.
Its suite fixture owns the queue. The case calls `pollOne()` explicitly and uses
`yield()` to give other ready cases a turn. Replace that progress function with
`io_context::poll_one()`, an engine tick, or another executor's operation.

A callback that resumes an awaiter may run while a different case is polling the
loop. Use a guarded continuation for that handoff so Gentest restores the owning
case's context. Calling a raw coroutine handle from the loop does not provide
that context boundary.

## 7. Adapt a worker callback with an ordinary awaiter

[07_worker.hpp](../examples/async/07_worker.hpp) contains a complete worker loop
and a `Read` awaiter. Its fixture drains queued work and joins the worker at
destruction. The awaiter uses only `await_ready()`, `await_suspend(handle)`, and
`await_resume()`. The same `Read` works directly and inside `wait_for()`.

The callback handoff has four steps:

1. In `await_suspend()`, obtain a guarded continuation with
   `handle.promise().make_continuation()` or `make_continuation(handle, reason)`.
2. Bind a `noexcept` result receiver with
   `bind_continuation<Args...>(next, receiver)`. The receiver stores the result
   for `await_resume()`.
3. Give the bound callback to the backend. The backend can invoke it on any
   thread, including inline during registration. Gentest owns the arguments
   and delivers the receiver on the runner with the owning case's context.
4. Return a value or throw an application exception from `await_resume()`.
   Make assertions in the resumed case.

The worker example passes a status and `std::unique_ptr<int>` through this
handoff. It also passes `next.stop_token()` to the worker. If stop is requested,
the worker reports a cancelled completion rather than dropping the callback.
No adopted test context is needed for this result handoff.

Backends choose their callback storage, such as `std::function<bool(Reply)>` or
`std::function<void()>`. A bound callback returns whether it was queued; a void
callback wrapper may discard that boolean. The backend still owns its queue,
synchronization, cancellation, and shutdown policy.

## 8. Acknowledge stop, then release the registration

[08_callback_cancellation.hpp](../examples/async/08_callback_cancellation.hpp)
models a read with no incoming reply. It registers a `std::stop_callback` on the
continuation's token. A stop request makes the backend complete the read; the
resumed awaiter throws `operation_cancelled`. Its destructor then removes the
registration. The case verifies this cleanup has finished when `when_any()`
returns. It also uses the unchanged awaiter with an expired `wait_for()`.

Cancellation must let the operation finish. Merely clearing the callback leaves
its coroutine suspended, so a composition cannot join it. There is no required
`cancel_wait()` method: ordinary scoped ownership handles registration cleanup.
Cancel one operation; do not stop an executor shared with other cases.

The example backend runs on the runner thread. A threaded backend must also
synchronize callback storage and keep buffers alive until its I/O has stopped.
A late guarded callback can safely submit owned arguments, but it cannot publish
into an abandoned coroutine. Rejected arguments may be destroyed on the producer
thread.

## Advanced: continuation lifetime

A `continuation_handle` represents one suspended operation. Each
`make_continuation()` call starts a fresh generation and invalidates earlier
handles. Copy one handle when multiple callbacks compete to complete the same
operation. Repeated calls and competing copies queue at most one completion.
The returned boolean means queued, not delivered or successful.

An awaiter owns its handle's lifetime. If `await_suspend()` returns `false`,
cancel the handle first because there is no suspension to resume. If registration
throws, cancel before rethrowing and release any backend resources it acquired.
Keep a local copy until registration succeeds, as the worker example does.
Ordinary awaiters returning a coroutine handle from `await_suspend()` need no
Gentest-specific hook.

`continuation_handle::cancel()` retires that handle and its copies. It does not
cancel the backend or resume the coroutine. Backend cancellation still needs a
completion path if an enclosing operation is waiting to join it.

Callback arguments are stored by value; reference parameters are copied into
owned storage. Move-only arguments work. `std::reference_wrapper` explicitly
borrows its referent, which must outlive delivery. Binding and registration may
allocate and throw. An exception during nonthrowing callback dispatch, including
payload transport or queue allocation, terminates.

A live guarded continuation keeps Gentest waiting for external work. Completing,
cancelling, or dropping the last handle wakes the runner. A promise or event alone
does not register external worker liveness. Retain a continuation while external
work exists, or bound the wait with a deadline. A suspended case with no ready
work, timers, live external continuations, or adopted work is unresumable.

## Advanced: an application-owned executor

A fixture can borrow an executor that already belongs to the application. Expose
a static setter, call it from `main()` before `run_all_tests()`, and keep the
executor alive until that call returns:

```cpp
struct [[gentest::fixture(global)]] Infrastructure {
    static void setExecutor(ApplicationExecutor &value) { executor = &value; }
    inline static ApplicationExecutor *executor = nullptr;
};

int main(int argc, char **argv) {
    ApplicationExecutor executor;
    Infrastructure::setExecutor(executor);
    return gentest::run_all_tests(argc, argv);
}
```

Here `ApplicationExecutor` is the application's existing type. Cases receive
`Infrastructure&` as a fixture parameter. Link `gentest::gentest_runtime` when
providing this `main()`, instead of linking `gentest::gentest_main`. This is an
optional ownership arrangement, not a registration requirement for async tests.

## Execution and output

Ready continuations use FIFO order. Timers are checked before ready work is
pumped and when waiting for progress; they cannot preempt a running case.
Fixture groups drain before the next group starts. Do not make a case depend on
a later group. An event with no possible producer fails as unresumable;
`promise::set_blocked()` explicitly produces a `BLOCKED` outcome instead.

Assertions, failures, skips, and mock configuration belong to the owning case.
Use `gentest::log()` for output. A worker needs an adopted context only when it
wants to log or observe that context's cooperative stop; guarded continuations
alone need no context adoption. See the
[context adoption contract](../README.md#threadscoroutines--logging).

Live output distinguishes `RUNNING`, `YIELDED`, and `SUSPENDED` cases.
`--async-log-tail=N` controls its recent log lines. `GENTEST_ASYNC_LIVE=0` selects
plain output; CI and captured output already use that mode.

All public async APIs are available from `gentest/async.h` or `import gentest;`.
The implementation is native Gentest code under BSL-1.0; consuming these APIs
does not require another coroutine library.
