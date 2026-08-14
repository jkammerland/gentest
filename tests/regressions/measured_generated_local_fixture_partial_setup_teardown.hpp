#pragma once

#include "gentest/runner.h"

#include <atomic>

namespace regressions::measured_generated_local_fixture_partial_setup_teardown {

extern std::atomic<bool> g_bench_started;
extern std::atomic<int>  g_bench_teardown_count;
extern std::atomic<bool> g_jitter_started;
extern std::atomic<int>  g_jitter_teardown_count;

struct BenchFirstFixture : gentest::FixtureSetup, gentest::FixtureTearDown {
    void setUp() override { g_bench_started.store(true, std::memory_order_relaxed); }
    void tearDown() override { g_bench_teardown_count.fetch_add(1, std::memory_order_relaxed); }
};
struct BenchSecondFixture : gentest::FixtureSetup {
    void setUp() override { gentest::asserts::EXPECT_TRUE(false, "generated-bench-second-setup-failed"); }
};
struct JitterFirstFixture : gentest::FixtureSetup, gentest::FixtureTearDown {
    void setUp() override { g_jitter_started.store(true, std::memory_order_relaxed); }
    void tearDown() override { g_jitter_teardown_count.fetch_add(1, std::memory_order_relaxed); }
};
struct JitterSecondFixture : gentest::FixtureSetup {
    void setUp() override { gentest::asserts::EXPECT_TRUE(false, "generated-jitter-second-setup-failed"); }
};

[[using gentest: bench("regressions/measured_generated_local_fixture_partial_setup_teardown/bench")]]
void bench_partial_setup_should_unwind(BenchFirstFixture &, BenchSecondFixture &);
[[using gentest: jitter("regressions/measured_generated_local_fixture_partial_setup_teardown/jitter")]]
void jitter_partial_setup_should_unwind(JitterFirstFixture &, JitterSecondFixture &);

} // namespace regressions::measured_generated_local_fixture_partial_setup_teardown
