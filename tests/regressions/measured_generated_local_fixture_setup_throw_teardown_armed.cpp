#include "measured_generated_local_fixture_setup_throw_teardown_armed.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>

#if defined(GENTEST_EXPECT_NO_EXCEPTIONS)
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
#error "GENTEST_EXPECT_NO_EXCEPTIONS requires exceptions to be disabled for this TU"
#endif
#endif

namespace regressions::measured_generated_local_fixture_setup_throw_teardown_armed {

std::atomic<bool> g_bench_setup_entered{false};
std::atomic<int>  g_bench_teardown_count{0};
std::atomic<bool> g_jitter_setup_entered{false};
std::atomic<int>  g_jitter_teardown_count{0};

void bench_setup_throw_should_teardown(BenchFixture &) {}
void jitter_setup_throw_should_teardown(JitterFixture &) {}

struct TeardownGuard final {
    ~TeardownGuard() {
        if (g_bench_setup_entered.load(std::memory_order_relaxed) && g_bench_teardown_count.load(std::memory_order_relaxed) != 1) {
            (void)std::fputs("regression marker: generated bench teardown not armed before setup\n", stderr);
            (void)std::fflush(stderr);
            std::abort();
        }
        if (g_jitter_setup_entered.load(std::memory_order_relaxed) && g_jitter_teardown_count.load(std::memory_order_relaxed) != 1) {
            (void)std::fputs("regression marker: generated jitter teardown not armed before setup\n", stderr);
            (void)std::fflush(stderr);
            std::abort();
        }
    }
};

[[maybe_unused]] TeardownGuard kTeardownGuard{};

} // namespace regressions::measured_generated_local_fixture_setup_throw_teardown_armed
