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

Use the usual `gentest::gentest_main` entry point. Fixtures own each service or
loop; no application runner or event-loop registration is required.

| Stage | Source | What it shows |
| --- | --- | --- |
| 1 | [Tasks](01_tasks.hpp) | `task<T>`, a returned value, and `yield()` |
| 2 | [Composition](02_composition.hpp) | `when_all()` results and `when_any()` stopping and joining a loser |
| 3 | [Cancellation](03_cancellation.hpp) | Deadlines, explicit stop tokens, and cleanup waits |
| 4 | [Fixtures](04_fixtures.hpp) | Async setup/teardown and suite-owned infrastructure |
| 5 | [Events and futures](05_events.hpp) | Named typed events, latched observations, and move-only one-shot values |
| 6 | [Polling](06_polling.hpp) | A complete user loop with explicit progress and cooperative yield |
| 7 | [Worker callbacks](07_worker.hpp) | A complete worker loop and an ordinary awaiter, including bounded waits |
| 8 | [Callback cancellation](08_callback_cancellation.hpp) | Acknowledging stop, finishing the wait, and scoped registration cleanup |

For a focused run:

```sh
build/examples/async/gentest_async --run=async_examples/composition/first_result_joins_loser --no-color
build/examples/async/gentest_async --filter=async_examples/worker/* --no-color
```

`when_all()` returns an owning tuple in input order. `when_any()` returns an
owning variant for the first completed input, after requesting stop for the
others and waiting for them to finish. Both accept ordinary awaitables; lvalue
inputs are borrowed and rvalues are owned. Void results use `std::monostate`.

`wait_for()` and `wait_until()` also accept ordinary awaitables. They request
stop and join before returning a timeout, so a backend that needs time to stop
can extend the wait beyond the deadline. An already-ready value still wins an
expired deadline. Event, future, sleep, and yield waits inherit cancellation;
an explicit token overrides it, and an empty token permits cleanup waits.

The worker awaiter uses the standard `await_ready`, `await_suspend`, and
`await_resume` methods. `make_continuation()` and `bind_continuation<Args...>()`
hand results back to the runner with the owning case's context. Backend status
is an ordinary callback argument; `await_resume()` interprets it. The same
awaiter works with a deadline without extra methods.

A cancellation callback must let the operation finish. Stage 8 completes its
read when stop is requested, then releases the registration after resumption.
It does not stop a shared executor. Backend callback storage and borrowed
arguments must remain alive until backend access ends; these examples send
owned arguments through callbacks.

See [the async guide](../../docs/async.md) for the complete contract, advanced
continuation lifetime rules, and an optional application-owned executor pattern.
