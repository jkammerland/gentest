# Async cases, one step at a time

This is one runnable project of coroutine cases. Each numbered header adds one
concept. Every case also passes on its own, in a shuffled run and when
repeated.

Build it against an installed gentest (see the [examples index](../README.md)
for requirements):

```bash
cmake -S examples/async -B build/examples/async -G Ninja -DCMAKE_PREFIX_PATH=/path/to/gentest/install
cmake --build build/examples/async
ctest --test-dir build/examples/async --output-on-failure
build/examples/async/gentest_async --repeat=2 --shuffle --seed 123 --no-color
```

The example uses the usual `gentest::gentest_main` entry point. Fixtures own
each service or loop; no application runner or event-loop registration is
needed.

## Stages

Each stage's cases share a suite, named after the header's namespace. Use the
suite to run one stage with `--filter`, for example
`--filter=async_examples/waits/*`.

| Stage | Source | Suite | What it shows |
| --- | --- | --- | --- |
| 1 | [Awaitables](01_awaitables.hpp) | `async_examples/cases` | `awaitable<T>`, a returned value, and `yield()` |
| 2 | [Composition](02_composition.hpp) | `async_examples/composition` | `when_all()` results, and `when_any()` stopping and joining the loser |
| 3 | [Cancellation](03_cancellation.hpp) | `async_examples/waits` | Deadlines, explicit stop tokens and cleanup waits |
| 4 | [Fixtures](04_fixtures.hpp) | `async_examples/fixtures` | Async setup and teardown, and suite-owned infrastructure |
| 5 | [Events and futures](05_events.hpp) | `async_examples/events` | Named typed events, latched observations and move-only one-shot values |
| 6 | [Polling](06_polling.hpp) | `async_examples/polling` | A complete user loop with explicit progress and cooperative yield |
| 7 | [Worker callbacks](07_worker.hpp) | `async_examples/worker` | A complete worker loop and an ordinary awaiter, including bounded waits |
| 8 | [Callback cancellation](08_callback_cancellation.hpp) | `async_examples/cancellation` | Acknowledging stop, finishing the wait and cleaning up the registration |

To run one case, or one stage:

```bash
build/examples/async/gentest_async --run=async_examples/composition/first_result_joins_loser --no-color
build/examples/async/gentest_async --filter='async_examples/worker/*' --no-color
```

[expected_tests.txt](expected_tests.txt) lists all 16 case names.

## Learn more

[Async tests](../../docs/async.md) walks through the same eight stages and
gives the full rules: composition results and ownership, deadlines and
cancellation, handing callback results back to the runner, continuation
lifetimes, and an optional application-owned executor.
