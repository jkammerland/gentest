#include "gentest/runner.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace regressions::measured_generated_local_fixture_setup_throw_teardown_armed {

std::atomic<bool> g_bench_setup_entered{false};
std::atomic<int>  g_bench_teardown_count{0};
std::atomic<bool> g_jitter_setup_entered{false};
std::atomic<int>  g_jitter_teardown_count{0};

struct BenchFixture : gentest::FixtureSetup, gentest::FixtureTearDown {
    void setUp() override {
        g_bench_setup_entered.store(true, std::memory_order_relaxed);
        throw std::runtime_error("generated-bench-setup-throws");
    }
    void tearDown() override { g_bench_teardown_count.fetch_add(1, std::memory_order_relaxed); }
};

struct JitterFixture : gentest::FixtureSetup, gentest::FixtureTearDown {
    void setUp() override {
        g_jitter_setup_entered.store(true, std::memory_order_relaxed);
        throw std::runtime_error("generated-jitter-setup-throws");
    }
    void tearDown() override { g_jitter_teardown_count.fetch_add(1, std::memory_order_relaxed); }
};

[[using gentest: bench("regressions/measured_generated_local_fixture_setup_throw_teardown_armed/bench")]]
void bench_setup_throw_should_teardown(BenchFixture &) {}

[[using gentest: jitter("regressions/measured_generated_local_fixture_setup_throw_teardown_armed/jitter")]]
void jitter_setup_throw_should_teardown(JitterFixture &) {}

struct TeardownGuard final {
    ~TeardownGuard() {
        if (g_bench_setup_entered.load(std::memory_order_relaxed) &&
            g_bench_teardown_count.load(std::memory_order_relaxed) != 1) {
            std::fputs("regression marker: generated bench teardown not armed before setup\n", stderr);
            std::fflush(stderr);
            std::abort();
        }
        if (g_jitter_setup_entered.load(std::memory_order_relaxed) &&
            g_jitter_teardown_count.load(std::memory_order_relaxed) != 1) {
            std::fputs("regression marker: generated jitter teardown not armed before setup\n", stderr);
            std::fflush(stderr);
            std::abort();
        }
    }
};

[[maybe_unused]] TeardownGuard kTeardownGuard{};

} // namespace regressions::measured_generated_local_fixture_setup_throw_teardown_armed
