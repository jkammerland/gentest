# Fixtures

A fixture is an object that gentest creates, sets up, passes to a case as a
parameter and tears down afterwards. Use fixtures for setup that many cases
need, either fresh for every case or shared by a group of cases.

## A first example

A fixture is an ordinary type. Name it as a parameter of a test, benchmark or
jitter function and gentest provides an instance. This header uses a fixture of
each lifetime:

```cpp
#pragma once

#include "gentest/runner.h"

#include <cstddef>
#include <string>
#include <vector>

namespace shop {

// Global fixture: one instance for the whole run.
struct [[gentest::fixture(global)]] Config : gentest::FixtureSetup {
    std::string currency;

    void setUp() override { currency = "EUR"; }
};

// Suite fixture: one instance for the cases in shop and its nested namespaces.
struct [[gentest::fixture(suite)]] Catalog : gentest::FixtureSetup, gentest::FixtureTearDown {
    std::vector<int> prices;

    void setUp() override { prices = {10, 20}; }
    void tearDown() override { prices.clear(); }
};

// Local fixture (no attribute): a fresh instance for every case.
struct Cart {
    std::vector<int> items;
};

namespace checkout {

[[gentest::test]]
inline void add_item(Cart &cart, Catalog &catalog) {
    cart.items.push_back(catalog.prices.front());
    gentest::expect_eq(cart.items.size(), std::size_t{1});
}

[[gentest::test]]
inline void starts_empty(Cart &cart, Config &config) {
    gentest::expect_true(cart.items.empty());
    gentest::expect_eq(config.currency, "EUR");
}

} // namespace checkout
} // namespace shop
```

`add_item` and `starts_empty` each get their own `Cart`. `Catalog` and `Config`
are set up when the first case that needs them runs, and torn down once at the
end of the run. `gentest::FixtureSetup` and `gentest::FixtureTearDown` come from
`gentest/runner.h`. [examples/fixtures](../examples/fixtures) is a complete
project with a local and a suite fixture.

## Lifetimes

| Lifetime | Declared with | Instances |
|---|---|---|
| local | no attribute | A fresh instance every time the case runs, including each [occurrence](command_line.md) under `--repeat`. |
| suite | `[[gentest::fixture(suite)]]` on the type | One instance per run, shared by the cases in the fixture's namespace and its nested namespaces. |
| global | `[[gentest::fixture(global)]]` on the type | One instance per run, shared by every case that uses it. |

Suite and global fixtures are called shared fixtures. Cases share them within
one runner invocation. Each [supervised death test](death_tests.md) runs in a
fresh process.

## Fixture parameters

Codegen decides what each parameter of a case is:

1. A parameter named in `parameters`, `parameters_pack`, `range`, `linspace`,
   `geom` or `logspace` receives generated values; see
   [Parameters and templates](parameters.md).
2. A parameter with a default argument keeps its default.
3. Every other parameter is a fixture.

Declare a fixture parameter in one of these forms:

| Parameter | What the case receives |
|---|---|
| `T&` | A reference to the instance. |
| `T*` | A pointer to the instance. |
| `std::shared_ptr<T>` | Shared ownership of the instance. |

A `std::unique_ptr<T>` parameter fails to compile; `std::unique_ptr` is a
return type of [allocation hooks](fixtures_allocation.md), not a parameter
form.

Type aliases resolve to the type they name: `Db&` with `using Db = Database;`
and `DbPtr` with `using DbPtr = std::shared_ptr<Database>;` both use the
`Database` fixture. A case can take several fixtures of any lifetimes, and
value parameters can sit between fixture parameters.

By default gentest creates each instance with `new T()`, so a fixture type needs
a default constructor. For constructor arguments, your own deleter or shared
ownership, add an [allocation hook](fixtures_allocation.md).

Fixtures come only from parameters: codegen rejects a `fixtures(...)` attribute
with `'fixtures(...)' was removed; infer fixtures from function parameters instead`.

## Where shared fixtures are visible

A shared fixture is visible to the cases in the namespace that declares it and
in the namespaces nested inside it. Declare it in the closest namespace that
contains all the cases that use it. A case outside that namespace fails codegen
with `fixture '<type>' is not in an ancestor namespace of this test`.

A suite fixture belongs to its namespace, so it must be declared inside a named
namespace. At global scope, codegen reports
`'fixture(suite)' requires an enclosing named namespace to derive a suite path`.
A global fixture may be declared at global scope; it is then visible to every
case.

Declare fixture types in headers, like cases. Codegen rejects a fixture
attribute in a `.cpp` file with
`Gentest fixture attributes in an authored .cpp are not supported by header-declaration registration; annotate the header definition instead`.
With module registration (`MODULE_REGISTRATION`), export the fixture types that
cases use; see [Named modules](modules.md).

## Setup and teardown

Derive a fixture from `gentest::FixtureSetup`, `gentest::FixtureTearDown` or
both, and override `setUp()` and `tearDown()`. Put work that can fail there
rather than in the constructor and destructor: the hooks can use assertions,
and gentest reports their failures.

For coroutine setup, derive from `gentest::AsyncFixtureSetup` or
`gentest::AsyncFixtureTearDown` instead; their `setUp()` and `tearDown()` return
`gentest::awaitable<void>`. Shared fixtures run these hooks for any case. A
local fixture runs them only when the case is itself a coroutine: a synchronous
case silently skips them, so give a local fixture that synchronous cases use
`gentest::FixtureSetup` or `gentest::FixtureTearDown`. An async case can take at
most one shared fixture. See [Async tests](async.md).

Each time a case runs, gentest:

1. Gets the case's shared fixtures, setting up any that aren't set up yet, and
   creates its local fixtures.
2. Calls `setUp()` on each local fixture, in parameter order.
3. Runs the case.
4. Calls `tearDown()` on the local fixtures in reverse order, then destroys
   them.

A local fixture's `tearDown()` runs even when the case or the fixture's own
`setUp()` fails. Local fixtures after a failed `setUp()` are never set up, so
they aren't torn down either.

If fail-fast cancels an async case during setup or the case body, the local
fixtures that entered setup run their normal teardown hooks, including timer
waits. If cancellation interrupts a teardown hook, gentest abandons the
suspended hook and attempts each remaining hook once in reverse order. Those
hooks can complete immediately ready work. A hook that waits for a timer or
external completion, or keeps yielding indefinitely, gives way to the next
fixture.

Benchmark and jitter cases set up their fixtures before the timed loop and tear
them down after it; see [Benchmarks and jitter](benchmarks.md).

## When shared fixtures are set up

- A shared fixture is created and set up, or activated, the first time a
  selected case needs it, before that case starts and outside any benchmark or
  jitter timing. A run that never needs a fixture doesn't create it, so a
  broken or slow fixture doesn't affect a selection that doesn't use it.
- A shared fixture is activated at most once per run and kept across `--repeat`
  occurrences. If activation fails, gentest doesn't retry it in that run.
- At the end of the run, gentest tears down each activated fixture once, in
  reverse activation order, and destroys each instance right after its
  `tearDown()`. This includes fixtures of cases that skipped at run time.
- A [static skip](assertions.md) acquires no fixtures.
- [Hand-written cases](runner_api.md) that name a fixture in their `gentest::Case`
  metadata follow the same rules.

Each run starts with no shared fixtures, so another `gentest::run_all_tests()`
call in the same process creates fresh ones. `gentest_discover_tests()` runs
every case in its own process, so under CTest each process sets up only the
shared fixtures its case needs.

For setup that every invocation needs, whatever the selection, write your own
`main()` that does the work before calling `gentest::run_all_tests()` or
`gentest::run_cases()`, and link `gentest::gentest` instead of
`gentest::gentest_main`; see [Embedding the runner](runner_api.md).

## When a fixture fails

| What fails | What happens |
|---|---|
| Creating a local fixture | The case fails, for example with `fixture allocation failed for '<type>': returned null` when an allocation hook returns null. |
| A local `setUp()` | The case fails, and the fixture's `tearDown()` runs. |
| Creating a shared fixture | gentest prints, for example, `gentest: fixture '<type>' fixture allocation returned null`. Every case that needs the fixture is reported as [blocked](reports.md), and the run fails. There is no instance to tear down. |
| A shared `setUp()` | gentest prints `gentest: fixture '<type>' fixture setup failed: <reason>`. Every case that needs the fixture is blocked, and the run fails. The instance is torn down at the end of the run. |
| A shared `tearDown()` | gentest prints `gentest: fixture teardown failed for <type>: <reason>`, and the run fails. |

Conflicting registrations of one shared fixture, for example with two
different lifetimes, are an error for the whole run.

## Dependencies between shared fixtures

The setup of one shared fixture can use another shared fixture. The only way to
do this is an unstable internal API, `gentest::detail::get_shared_fixture_typed`,
which can change in any release:

```cpp
#pragma once

#include "gentest/detail/generated_runtime.h"
#include "gentest/runner.h"

#include <memory>
#include <string>

namespace app {

struct [[gentest::fixture(global)]] Database : gentest::FixtureSetup {
    bool connected = false;

    void setUp() override { connected = true; }
};

struct [[gentest::fixture(suite)]] Cache : gentest::FixtureSetup {
    std::shared_ptr<Database> db;

    void setUp() override {
        using gentest::detail::SharedFixtureScope;
        std::string error;
        // Unstable internal API. Arguments: the lifetime, the requesting suite
        // and the fixture's qualified name.
        db = gentest::detail::get_shared_fixture_typed<Database>(SharedFixtureScope::Global, "app", "app::Database", error);
        gentest::require(db != nullptr, error);
    }
};

[[gentest::test]]
inline void cached_lookup(Cache &cache) {
    gentest::expect(cache.db->connected, "the database is set up before the cache");
}

} // namespace app
```

- The dependency is activated first, so it is ready inside the dependent's
  `setUp()`. It is torn down after the dependent is torn down and destroyed,
  so the dependent can use it until then.
- Asking for a fixture whose setup is in progress, for example in a dependency
  cycle, returns a null pointer with the error
  `fixture initialization in progress`.
- No fixture is activated during teardown.
- Activating a fixture for the first time inside a timed benchmark or jitter
  call fails with
  `fixture initialization is not allowed during a measured call; acquire it during setup`.
  Acquire fixtures in setup, or give a hand-written case its fixture metadata.

## Case order with shared fixtures

Within a suite, the cases without shared fixtures run first. The cases with
shared fixtures follow, grouped by their first shared fixture parameter: the
groups of suite fixtures, then the groups of global fixtures. `--shuffle`
shuffles the cases within each group and never mixes groups; see
[Running, debugging, and replaying tests](runner_workflows.md).

## Member tests

Member tests are deprecated; see [DEPRECATIONS.md](../DEPRECATIONS.md). Write
a free function with fixture parameters instead. In a member test, the class is
the fixture: an unannotated class gets a fresh instance for each member test,
and with `fixture(suite)` or `fixture(global)` the member tests share one
instance. Other fixtures come from the member function's parameters, as for a
free function.
