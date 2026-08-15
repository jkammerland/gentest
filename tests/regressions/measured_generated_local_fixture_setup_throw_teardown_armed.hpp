#pragma once

#include "gentest/runner.h"

#include <atomic>

namespace regressions::measured_generated_local_fixture_setup_throw_teardown_armed {

extern std::atomic<bool> g_bench_setup_entered;
extern std::atomic<int>  g_bench_teardown_count;
extern std::atomic<bool> g_jitter_setup_entered;
extern std::atomic<int>  g_jitter_teardown_count;

struct BenchFixture : gentest::FixtureSetup, gentest::FixtureTearDown {
    void setUp() override {
        g_bench_setup_entered.store(true, std::memory_order_relaxed);
        gentest::asserts::EXPECT_TRUE(false, "generated-bench-setup-throws");
    }
    void tearDown() override { g_bench_teardown_count.fetch_add(1, std::memory_order_relaxed); }
};
struct JitterFixture : gentest::FixtureSetup, gentest::FixtureTearDown {
    void setUp() override {
        g_jitter_setup_entered.store(true, std::memory_order_relaxed);
        gentest::asserts::EXPECT_TRUE(false, "generated-jitter-setup-throws");
    }
    void tearDown() override { g_jitter_teardown_count.fetch_add(1, std::memory_order_relaxed); }
};

[[using gentest: bench("regressions/measured_generated_local_fixture_setup_throw_teardown_armed/bench")]]
void bench_setup_throw_should_teardown(BenchFixture &);
[[using gentest: jitter("regressions/measured_generated_local_fixture_setup_throw_teardown_armed/jitter")]]
void jitter_setup_throw_should_teardown(JitterFixture &);

} // namespace regressions::measured_generated_local_fixture_setup_throw_teardown_armed
