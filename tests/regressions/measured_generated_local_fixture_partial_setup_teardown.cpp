#include "gentest/runner.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>

#if defined(GENTEST_EXPECT_NO_EXCEPTIONS)
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
#error "GENTEST_EXPECT_NO_EXCEPTIONS requires exceptions to be disabled for this TU"
#endif
#endif

namespace regressions::measured_generated_local_fixture_partial_setup_teardown {

std::atomic<bool> g_bench_started{false};
std::atomic<int>  g_bench_teardown_count{0};
std::atomic<bool> g_jitter_started{false};
std::atomic<int>  g_jitter_teardown_count{0};

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
void bench_partial_setup_should_unwind(BenchFirstFixture &, BenchSecondFixture &) {}

[[using gentest: jitter("regressions/measured_generated_local_fixture_partial_setup_teardown/jitter")]]
void jitter_partial_setup_should_unwind(JitterFirstFixture &, JitterSecondFixture &) {}

struct TeardownGuard final {
    ~TeardownGuard() {
        if (g_bench_started.load(std::memory_order_relaxed) && g_bench_teardown_count.load(std::memory_order_relaxed) != 1) {
            (void)std::fputs("regression marker: generated bench local teardown missing after setup failure\n", stderr);
            (void)std::fflush(stderr);
            std::abort();
        }
        if (g_jitter_started.load(std::memory_order_relaxed) && g_jitter_teardown_count.load(std::memory_order_relaxed) != 1) {
            (void)std::fputs("regression marker: generated jitter local teardown missing after setup failure\n", stderr);
            (void)std::fflush(stderr);
            std::abort();
        }
    }
};

[[maybe_unused]] TeardownGuard kTeardownGuard{};

} // namespace regressions::measured_generated_local_fixture_partial_setup_teardown
