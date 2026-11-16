# Embedding the runner

Run gentest from your own program: write your own `main()` around the
generated cases, or give the runner a table of hand-written cases instead of
generated ones.

## Write your own main()

`gentest::gentest_main` provides a `main()` that calls
`gentest::run_all_tests(argc, argv)`. To run your own code before or after the
tests, link `gentest::gentest` instead and write `main()` yourself:

```cpp
#include "gentest/runner.h"

int main(int argc, char **argv) {
    // Process-wide setup that every invocation needs goes here.
    return gentest::run_all_tests(argc, argv);
}
```

```cmake
add_executable(my_tests main.cpp cases.hpp)
target_compile_features(my_tests PRIVATE cxx_std_20)
target_link_libraries(my_tests PRIVATE gentest::gentest)
gentest_attach_codegen(my_tests)
```

Link `gentest::gentest`, not just `gentest::gentest_runtime`: it also adds the
compile options that keep the compiler from warning about the unknown `gentest`
attributes on your cases. With `import gentest;`, see
[Named modules](modules.md) for the targets to link.

`run_all_tests()` parses the [command line](command_line.md), runs the
generated cases and returns the exit code. Setup that only some cases need
belongs in a [fixture](fixtures.md).

## Run hand-written cases

A **hand-written case** is a `gentest::Case` descriptor that you write yourself
instead of letting `gentest_codegen` generate it from an attribute. Pass a
table of them, together with the command-line arguments, to
`gentest::run_cases()`. This `main.cpp` runs one hand-written case:

```cpp
#include "gentest/runner.h"

#include <string_view>
#include <vector>

using namespace gentest::asserts;

namespace {

void check_sum(void *) {
    EXPECT_EQ(2 + 2, 4);
}

constexpr std::string_view kTags[]         = {"fast"};
constexpr std::string_view kRequirements[] = {"SSR-010"};

const gentest::Case kCases[] = {
    {
        .name             = "embedded/check_sum",
        .fn               = &check_sum,
        .file             = __FILE__,
        .line             = __LINE__,
        .tags             = kTags,
        .requirements     = kRequirements,
        .skip_reason      = {},
        .should_skip      = false,
        .fixture          = {},
        .fixture_lifetime = gentest::FixtureLifetime::None,
        .suite            = "embedded",
    },
};

} // namespace

int main(int argc, char **argv) {
    std::vector<const char *> args(argv, argv + argc);
    return gentest::run_cases(kCases, args);
}
```

The program needs no code generation:

```cmake
add_executable(embedded_runner main.cpp)
target_compile_features(embedded_runner PRIVATE cxx_std_20)
target_link_libraries(embedded_runner PRIVATE gentest::gentest)
```

`./embedded_runner` runs `embedded/check_sum`, and every runner option works
as it does for a generated test executable. Assertions, `gentest::log()`,
`gentest::skip()` and the other case APIs work inside `fn`.

## Arguments and exit codes

`run_cases()` and the span overload of `run_all_tests()` read their arguments
like a test executable reads its command line:

- When the first argument doesn't start with `-`, the runner takes it as the
  program name and skips it. Pass it: the runner uses it for the `Rerun:`
  commands it prints for failed cases (see
  [Running, debugging, and replaying tests](runner_workflows.md)) and to name
  Allure results (see [Reports](reports.md)).
- Null pointers are skipped. An empty span is the same as starting a test
  executable without options.

The return value is the exit code a test executable returns for the same cases
and arguments; see [Exit codes](command_line.md#exit-codes).

## How run_cases() uses your table

- The runner sorts a private, shallow copy of the table by name, then file and
  line, the same order as generated cases. It never changes your table.
- The cases are not registered: `gentest::registered_cases()`, which returns a
  sorted copy of the program's generated cases, doesn't include them.
- Each call restores the runner's log settings when it returns, so a program
  can call the runner more than once.
- Keep the functions and all storage that the `Case` string views and spans
  refer to alive until `run_cases()` returns.
- `gentest::Case` is a source-level interface, not a stable binary interface.
  Rebuild hand-written tables with the gentest version you link against.

## The gentest::Case fields

Designated initializers must follow the field order of this table, a C++20
rule. The fields without a default are the ones in the example above. If you
leave any of them out, it is value-initialized, but `-Wextra` warns about the
missing initializer.

| Field | Default | Meaning |
|---|---|---|
| `name` | none | Full case name, as `--run`, `--filter` and the reports use it. |
| `fn` | none | `void(void *)` function that runs the case. For a case without a shared fixture, the runner passes `nullptr`. |
| `file`, `line` | none | Source location for listings, failure summaries and annotations. |
| `is_benchmark` | `false` | Makes the case a bench case. |
| `is_jitter` | `false` | Makes the case a jitter case. |
| `is_baseline` | `false` | Marks a bench or jitter case as the baseline for the other measured cases; see [Benchmarks](benchmarks.md). |
| `tags` | none | Tags for `--tag`, listings and reports. A `death` tag makes the case a death test. The runner accepts any strings here. |
| `requirements` | none | Requirement IDs; see [Requirements traceability](traceability_standards.md). |
| `skip_reason`, `should_skip` | none | `should_skip = true` makes the case a [static skip](assertions.md) with that reason. |
| `fixture`, `fixture_lifetime` | none | The shared fixture the case uses. For a case without one, leave `fixture` empty and use `gentest::FixtureLifetime::None`. See [Use a shared fixture](#use-a-shared-fixture); [Test inventory](test_inventory.md) maps all values to fixture lifetimes. |
| `suite` | none | The suite the case belongs to; it orders the case and names its group in reports. |
| `async_fn`, `is_async` | `nullptr`, `false` | Set by generated code for coroutine cases. Leave them unset. |
| `items_per_call` | `1` | Logical items that one call of a bench or jitter case processes. |
| `owner` | empty | Owner of the case. |
| `benchmark` | calibrated, runner-timed | Execution options of a bench case; see the next section. |
| `reports_body_start` | `false` | Set by generated code. Leave it unset. |
| `measured_fn` | `nullptr` | For a bench or jitter case, the function that the timed calls run. |

## Use a shared fixture

A hand-written case can use a suite or global fixture that codegen registers:
a `fixture(suite)` or `fixture(global)` class in a header that
`gentest_attach_codegen()` scans for the same program. For example,
`tables.hpp`:

```cpp
#pragma once

#include "gentest/test.h"

namespace math {

struct [[using gentest: fixture(suite)]] Tables {
    int size = 8;
};

} // namespace math
```

Set `fixture` to the class's qualified name, `fixture_lifetime` to
`gentest::FixtureLifetime::MemberSuite` or `MemberGlobal`, and `suite` to a
suite in the fixture's namespace. The runner sets the fixture up on first use,
as for a generated case, and passes `fn` a pointer to the instance. In
`main.cpp`:

```cpp
#include "gentest/runner.h"
#include "tables.hpp"

#include <vector>

using namespace gentest::asserts;

void lookup(void *fixture) {
    const auto &tables = *static_cast<math::Tables *>(fixture);
    EXPECT_EQ(tables.size, 8);
}

const gentest::Case kCases[] = {
    {
        .name             = "math/lookup",
        .fn               = &lookup,
        .file             = __FILE__,
        .line             = __LINE__,
        .tags             = {},
        .requirements     = {},
        .skip_reason      = {},
        .should_skip      = false,
        .fixture          = "math::Tables",
        .fixture_lifetime = gentest::FixtureLifetime::MemberSuite,
        .suite            = "math",
    },
};

int main(int argc, char **argv) {
    std::vector<const char *> args(argv, argv + argc);
    return gentest::run_cases(kCases, args);
}
```

Add the header to the target and attach codegen so that the fixture is
registered:

```cmake
add_executable(embedded_runner main.cpp tables.hpp)
target_compile_features(embedded_runner PRIVATE cxx_std_20)
target_link_libraries(embedded_runner PRIVATE gentest::gentest)
gentest_attach_codegen(embedded_runner)
```

[Fixtures](fixtures.md) describes when shared fixtures are set up and torn
down.

## Measured hand-written cases

For a bench or jitter case, the runner calls `fn` in the setup phase, in every
timed call and in the teardown phase. Set `measured_fn` to keep setup and
teardown out of the timing: the timed calls then run `measured_fn`, and `fn`
runs only in setup and teardown.

`Case::benchmark` holds the options that the `bench_once`, `bench_time(...)`
and `bench_warmup(...)` attributes set for a generated case:

| Field | Values | Attribute |
|---|---|---|
| `execution` | `gentest::BenchmarkExecution::Calibrated` (default) or `Once` | `bench_once` |
| `time_source` | `gentest::BenchmarkTimeSource::Runner` (default) or `Supplied` | `bench_time("supplied")` |
| `warmup_epochs` | `std::optional<std::size_t>`; when empty, `--bench-warmup` applies | `bench_warmup(N)` |

This bench case runs its measured call once and supplies its own duration, so
building the input stays out of the result:

```cpp
#include "gentest/benchmark.h"
#include "gentest/runner.h"

#include <chrono>
#include <numeric>
#include <vector>

using namespace gentest::asserts;

namespace {

void no_setup(void *) {}

void sum_numbers(void *) {
    const std::vector<int> numbers(1'000'000, 1); // not timed
    const auto             start = std::chrono::steady_clock::now();
    const long long        sum   = std::accumulate(numbers.begin(), numbers.end(), 0LL);
    gentest::setBenchmarkDuration(std::chrono::steady_clock::now() - start);
    EXPECT_EQ(sum, 1'000'000LL);
}

const gentest::Case kCases[] = {
    {
        .name             = "embedded/sum_numbers",
        .fn               = &no_setup,
        .file             = __FILE__,
        .line             = __LINE__,
        .is_benchmark     = true,
        .tags             = {},
        .requirements     = {},
        .skip_reason      = {},
        .should_skip      = false,
        .fixture          = {},
        .fixture_lifetime = gentest::FixtureLifetime::None,
        .suite            = "embedded",
        .benchmark =
            {
                .execution     = gentest::BenchmarkExecution::Once,
                .time_source   = gentest::BenchmarkTimeSource::Supplied,
                .warmup_epochs = 0,
            },
        .measured_fn = &sum_numbers,
    },
};

} // namespace

int main(int argc, char **argv) {
    std::vector<const char *> args(argv, argv + argc);
    return gentest::run_cases(kCases, args);
}
```

The runner checks these rules for every selected case before any fixture
setup:

- Options other than the defaults need `is_benchmark = true`.
- `BenchmarkTimeSource::Supplied` needs `BenchmarkExecution::Once`.
- `BenchmarkExecution::Once` can't have a positive `warmup_epochs`.
- A bench case needs a positive `items_per_call`.

A case that breaks one of them stops the run with exit code 1 and an error
such as
`invalid benchmark metadata for <name>: supplied timing requires bench_once`.
See [Benchmarks](benchmarks.md) for `gentest::setBenchmarkDuration()`, timing
samples and how measured reports count items.

## Notifications and progress events

Pass a `gentest::RunConfiguration` as the last argument of `run_cases()` or of
either `run_all_tests()` overload. It installs a notifier and an optional
shared `gentest::FrameworkEventVisitor` for the run:

```cpp
run_cases(cases, arguments, configuration);
run_all_tests(arguments, configuration);
run_all_tests(argc, argv, configuration);
```

If the notification machinery fails, the call returns 1, even when every case
passed. See [Notifications](notifications.md) for the setup, what the
notifications cover and the source events.

## With `import gentest;`

`import gentest;` exports `run_cases`, `run_all_tests`, `registered_cases`,
`gentest::Case`, `gentest::FixtureLifetime`, the benchmark option types and
`gentest::RunConfiguration`. See [Named modules](modules.md).
