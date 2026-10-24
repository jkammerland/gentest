# Async cases, one step at a time

This is one runnable project. Each numbered header adds one concept; every case
also works on its own, in a shuffled run, and across repetitions.

Build against Gentest installed from this checkout, with a C++20 compiler and host Clang:

```sh
cmake -S examples/async -B build/examples/async -G Ninja \
  -DCMAKE_PREFIX_PATH=/path/to/gentest/install
cmake --build build/examples/async
ctest --test-dir build/examples/async --output-on-failure
build/examples/async/gentest_async --repeat=2 --shuffle --seed 123 --no-color
```

Use the usual `gentest::gentest_main` entry point. No application runner or
event-loop registration is required.

| Stage | Source | What it shows |
| --- | --- | --- |
| 1 | [Cases](01_cases.hpp) | `async_test<T>`, a returned value, and `yield()` |
| 2 | [Fixtures](02_fixtures.hpp) | Async setup/teardown and a shared suite fixture |
| 3 | [Events](03_events.hpp) | Named, typed, latched values without globals |
| 4 | [Waits](04_waits.hpp) | One-shot futures, deadlines, and handling a missing reply |
| 5 | [Polling](05_polling.hpp) | A complete user loop, explicitly driven by the case |
| 6 | [Worker callbacks](06_worker.hpp) | A complete threaded loop and an ordinary C++ awaiter |
| 7 | [Cancellation](07_cancellation.hpp) | Retiring one timed-out registration and observing stop |

Run one stage or case:

```sh
build/examples/async/gentest_async --filter='async_examples/events/*' --no-color
build/examples/async/gentest_async --run=async_examples/worker/bounded_callback --no-color
```

An `event<T>` slot keeps its value until reset. All observers of the same name
see that value; waiting does not dequeue it. Use `promise<T>`/`future<T>` for a
one-shot result, including a move-only value.

The two loop examples show the ownership boundary. A shared fixture owns the
user's infrastructure. The case drives a loop explicitly, or the fixture owns
its worker thread. An Asio `io_context` can be owned in exactly the same ways.
An application that already owns one can supply it through its own fixture's
static setter; Gentest does not need an injection API in `main()`.

The worker awaiter uses standard `await_ready`, `await_suspend`, and
`await_resume`. `make_continuation()` creates one completion capability;
`bind_continuation<Args...>()` packages callback arguments for delivery on the
runner thread. Its receiver must be `noexcept`. Backend status is an ordinary
argument; `await_resume()` interprets it. The optional `await_subscribe(next,
stop)` hook lets `wait_for`/`wait_until` compose the operation with a timer.
If registration throws, cancel the capability before rethrowing. If an awaiter
returns `false` from `await_suspend`, cancel before returning. The worker
example keeps a local copy for registration rollback.

A timeout requests stop and retires that wait. It does not stop a shared loop.
The adapter owns backend cancellation and resources: keep backend callback
storage alive until it is retired, and keep borrowed callback arguments alive
or turn them into owned values. The examples use owned values.

See [the async guide](../../docs/async.md) for the completion and deadline contract.
