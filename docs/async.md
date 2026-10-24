# Async tests

An async case returns `gentest::async_test<T>`. Gentest runs and resumes its
coroutines on one runner thread. Other ready cases in the same fixture group
can run while a case is suspended.

Start with the runnable [async examples](../examples/async/). They form one
project with seven short stages, complete backends, and no application globals.
Each case works independently and with repeat and shuffle enabled.

## 1. Cases and yield

[01_cases.hpp](../examples/async/01_cases.hpp) starts with a helper that returns a
value:

```cpp
#include "gentest/async.h"
#include "gentest/test.h"

inline auto answer() -> gentest::async_test<int> {
    co_await gentest::async::yield();
    co_return 42;
}

[[gentest::test("uses_answer")]]
inline auto usesAnswer() -> gentest::async_test<void> {
    const int value = co_await answer();
    gentest::expect_eq(value, 42);
}
```

`yield()` queues the current coroutine behind other ready work. `sleep_for()`
and `sleep_until()` suspend until a scheduler timer expires. Neither needs a
user event loop. Time points use `std::chrono::steady_clock`.

Awaiting a helper propagates its value or exception. Top-level return values
are discarded. Parameterized and template cases use the same async return type.
One helper task supports one active waiter; concurrent waits on the same task
are rejected.

## 2. Fixtures own shared infrastructure

[02_fixtures.hpp](../examples/async/02_fixtures.hpp) shows a local connection
with async setup/teardown and a shared server with async setup.

Implement `gentest::AsyncFixtureSetup::setUp()` and/or
`gentest::AsyncFixtureTearDown::tearDown()`, returning `async_test<void>`. An
unannotated fixture is local to a case. `[[gentest::fixture(suite)]]` shares a
fixture with consumers in its namespace and descendants;
`[[gentest::fixture(global)]]` gives it run lifetime.

Use these fixtures to own an Asio `io_context`, an executor, or another shared
service. If an application already owns the object, its fixture can expose a
static setter called from `main()`. The ordinary `gentest::gentest_main` entry
point needs no additional API when the fixture owns the object itself.

## 3. Named and typed events

[03_events.hpp](../examples/async/03_events.hpp) puts an
`event<Connected>` channel in a shared fixture. Tests await `wait("primary")`
and receive a `Connected&`, without communicating through globals.

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

## 4. Bound a wait and choose the outcome

[04_waits.hpp](../examples/async/04_waits.hpp) demonstrates one-shot values,
success, and timeout. Use `wait_for(operation, duration)` or
`wait_until(operation, deadline)` when the case owns a deadline:

```cpp
auto result = co_await gentest::async::wait_for(replies.wait("request-1"), 1s);
if (result.timed_out()) {
    gentest::fail("request-1 did not receive a reply");
    co_return;
}
gentest::expect_eq(result.value(), 42);
```

`1s` requires `using namespace std::chrono_literals;`. A timeout is a returned
status; the test decides whether it should fail, skip, or continue.

`wait_result<T>` provides `ready()`, `timed_out()`, `status()`, and a boolean
conversion. `value()` is valid only when ready. It preserves references from
events; `std::move(result).value()` extracts an owned value. A void operation
returns `wait_result<void>`.

Gentest primitives and helper tasks support timed waits directly. A custom
awaiter can opt in through the subscription hook described below. Timing out
retires only this wait; the event, future, or shared executor remains usable.
An unbounded external wait may last indefinitely. Retain a public continuation
while external work exists, or use a deadline when waiting on a worker's promise
or event. A promise/event alone does not register worker liveness with Gentest.

## 5. Drive your event loop

[05_polling.hpp](../examples/async/05_polling.hpp) contains a complete small loop.
Its shared fixture owns the queue; the case explicitly calls `pollOne()` and
uses `yield()` to give other ready cases a turn. Replace that progress function
with `io_context::poll_one()`, an engine tick, or another executor's operation.

[06_worker.hpp](../examples/async/06_worker.hpp) takes the other approach: the
fixture owns a loop with its own worker thread. Its destructor drains queued
work and joins the thread. Gentest does not drive or register either loop.

## 6. Adapt callbacks with ordinary C++ awaiters

[06_worker.hpp](../examples/async/06_worker.hpp) also contains the complete `Read`
awaiter. It uses standard `await_ready()`, `await_suspend(handle)`, and
`await_resume()`. There is no replacement awaiter protocol.

The public completion contract is:

1. Register the operation on the runner. Obtain one capability with
   `handle.promise().make_continuation()` or
   `gentest::async::make_continuation(handle, reason)`. Each factory call starts
   a fresh generation and invalidates earlier capabilities. **Copy that one
   capability** when callbacks compete to finish the same operation.
2. Bind a `noexcept` receiver using
   `gentest::async::bind_continuation<Args...>(next, receiver)`. The receiver
   stores the operation's result in the awaiter. Application status, error, or
   disconnect reasons are ordinary typed arguments.
3. Invoke the bound callback from any thread, including inline during
   registration. Gentest owns the arguments and queues the completion. It
   invokes the receiver and resumes the coroutine later on the runner, with
   the owning case's context restored.
4. Interpret the result in `await_resume()`, returning a value or throwing an
   application exception. Make assertions in the resumed case.

For example, the worker backend calls its bound callback with
`complete(Reply::value, std::make_unique<int>(42))`. The value crosses the queue;
the worker never writes into coroutine storage. No adopted test context is
needed for this handoff.

The concrete bound callback and `continuation_handle::try_invoke()` return
`bool`: true means queued, not delivered or successful before a deadline.
`continuation<Args...>` is optional move-only callback storage; erasing a bound
callback into it discards that boolean. Repeated calls and competing copies
admit at most one completion. Creating a new generation, canceling a case, or
closing the scheduler prevents stale publication and resumption. Capabilities
do not retain abandoned coroutine frames. Starting the next Gentest wait also
retires capabilities left by a caught registration exception.

An awaiter owns its capability's lifetime. If `await_suspend()` returns `false`,
cancel the capability first: there will be no suspension for it to resume.
If registration throws, cancel before rethrowing, as the worker example does.
Keep a local copy until registration succeeds. A callback queued inline is
still delivered later; a normal registration leaves the coroutine suspended.
These cleanup rules also apply when the next awaiter is ordinary C++ code that
does not register work with Gentest.

Callback arguments are stored by value; reference parameters are copied into
owned storage. Move-only arguments work. `std::reference_wrapper` explicitly
borrows its referent, which must outlive delivery. Backend callback storage must
be synchronized by the backend. Binding and registration may allocate and throw;
an exception during nonthrowing callback dispatch, including payload transport
or queue allocation, terminates. Rejected payloads may be destroyed on the
producer thread.

A live public capability keeps Gentest waiting for external work. Completing,
canceling, or dropping the last capability wakes the runner. `cancel()` retires
that capability and its copies; it does not itself cancel the backend or resume
the case. The runner reports a suspended case as unresumable when no ready work,
timers, live external capabilities, or adopted work can make progress.

## 7. Compose a custom operation with a timeout

[07_cancellation.hpp](../examples/async/07_cancellation.hpp) demonstrates a
pending backend registration, stop observation, and retirement.

To support `wait_for`/`wait_until`, add this optional method alongside the usual
awaiter methods:

```cpp
void await_subscribe(gentest::async::continuation_handle next,
                     std::stop_token stop);
```

The timeout wrapper supplies the capability shared with its timer competitor.
Bind that supplied capability for every backend completion reason. Do not call
`make_continuation()` inside this hook: a fresh generation would invalidate the
timeout competitor. The ordinary `await_suspend()` can obtain its own capability
and forward to the same hook, as the worker example does.

On timeout or case cancellation, Gentest requests the supplied stop token and
suppresses queued value publication. An optional `cancel_wait() noexcept` retires
the registration on the runner after a timeout, successful result extraction, or
destruction of an active timed wait. The awaiter's own destructor must also
retire any registration owned by an ordinary, untimed wait. Registration that
throws must clean up its own backend resources.

Stop is a cancellation request. The adapter/backend remains responsible for
buffers, callback storage, and I/O resources until backend access has ended.
Cancel one subscription; do not stop a context shared with other cases. A late
callback may submit its owned value safely, but it cannot publish into an
invalidated awaiter.

Deadline arbitration preserves these rules:

- An already-ready value wins at the await site, even with an expired deadline.
- For a suspended wait, admission before the deadline remains ready even if the
  runner delivers the value later.
- Admission at or after the deadline yields timeout without running the value
  receiver. A timeout competitor that wins first also yields timeout.

## Execution and output

Ready continuations use FIFO order. Timers are checked before ready work is
pumped and when waiting for progress. They cannot preempt a running case.
Fixture groups drain before the next group starts; do not make a case depend
on a later group. An event with no possible producer fails as unresumable;
`promise::set_blocked()` explicitly produces a `BLOCKED` outcome instead.

Assertions, failures, skips, and mock configuration belong to the owning case.
Use `gentest::log()` for output. A worker needs an adopted context only when it
wants to log or observe that context's cooperative stop; continuations alone
need no context adoption. See [the context adoption contract](../README.md#threadscoroutines--logging).

Live output distinguishes `RUNNING`, `YIELDED`, and `SUSPENDED` cases.
`--async-log-tail=N` controls its recent log lines. `GENTEST_ASYNC_LIVE=0` selects
plain output; CI and captured output already use that mode.

All public async APIs are available from `gentest/async.h` or `import gentest;`.
The implementation is native Gentest code under BSL-1.0; consuming these APIs
does not require coflow or another coroutine library.
