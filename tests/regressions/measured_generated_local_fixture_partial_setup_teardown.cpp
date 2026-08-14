#include "measured_generated_local_fixture_partial_setup_teardown.hpp"

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

void bench_partial_setup_should_unwind(BenchFirstFixture &, BenchSecondFixture &) {}
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
