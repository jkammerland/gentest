#pragma once

#include "gentest/runner.h"

#include <memory>
#include <vector>

namespace fixture_selection {

struct Counters {
    int              dependency_setup      = 0;
    int              dependency_teardown   = 0;
    int              suite_setup           = 0;
    int              suite_teardown        = 0;
    int              unrelated_allocations = 0;
    int              unrelated_setup       = 0;
    int              local_setup           = 0;
    int              local_teardown        = 0;
    int              bodies                = 0;
    std::vector<int> destruction_order;
};

extern Counters counts;

struct [[gentest::fixture(global)]] ZDependency : gentest::AsyncFixtureSetup, gentest::FixtureTearDown {
    bool alive = false;

    gentest::awaitable<void> setUp() override;
    void                     tearDown() override;
    ~ZDependency() override;
};

struct [[gentest::fixture(suite)]] ASelected : gentest::FixtureSetup, gentest::FixtureTearDown {
    std::shared_ptr<ZDependency> dependency;

    void setUp() override;
    void tearDown() override;
    ~ASelected() override;
};

struct [[gentest::fixture(suite)]] NestedFailure : ASelected {
    void setUp() override;
};

struct [[gentest::fixture(suite)]] Unrelated : gentest::FixtureSetup {
    Unrelated();
    void setUp() override;
};

struct Local : gentest::FixtureSetup, gentest::FixtureTearDown {
    void setUp() override;
    void tearDown() override;
};

[[gentest::test]] void                                 plain();
[[gentest::test]] void                                 selected(ASelected &, Local &);
[[gentest::test]] gentest::awaitable<void>             selected_async(ASelected &);
[[using gentest: test, skip]] void                     static_skip(Unrelated &);
[[gentest::test]] void                                 runtime_skip(ASelected &);
[[gentest::test]] void                                 unrelated(Unrelated &);
[[using gentest: bench("measured"), bench_once]] void  measured(ASelected &);
[[gentest::jitter("latency")]] void                    latency(ASelected &);
[[using gentest: bench("skipped_bench"), skip]] void   skipped_bench(Unrelated &);
[[using gentest: jitter("skipped_jitter"), skip]] void skipped_jitter(Unrelated &);

[[gentest::test]] void nested_failure(NestedFailure &);

} // namespace fixture_selection
