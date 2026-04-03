#include "gentest/runner.h"

#include <atomic>
#include <string_view>

namespace {

std::atomic<bool> g_bench_setup_entered{false};
std::atomic<int>  g_bench_teardown_count{0};
std::atomic<int>  g_bench_call_count{0};
std::atomic<bool> g_jitter_setup_entered{false};
std::atomic<int>  g_jitter_teardown_count{0};
std::atomic<int>  g_jitter_call_count{0};

struct BenchFixture : gentest::FixtureSetup, gentest::FixtureTearDown {
    void setUp() override { g_bench_setup_entered.store(true, std::memory_order_relaxed); }
    void tearDown() override {
        g_bench_teardown_count.fetch_add(1, std::memory_order_relaxed);
        gentest::skip("bench-teardown-skip-only-marker");
    }
};

struct JitterFixture : gentest::FixtureSetup, gentest::FixtureTearDown {
    void setUp() override { g_jitter_setup_entered.store(true, std::memory_order_relaxed); }
    void tearDown() override {
        g_jitter_teardown_count.fetch_add(1, std::memory_order_relaxed);
        gentest::skip("jitter-teardown-skip-only-marker");
    }
};

constexpr unsigned kBenchTeardownSkipLine = __LINE__ + 1;
void               bench_teardown_skip(void *) {
    const auto phase = gentest::detail::bench_phase();
    if (phase != gentest::detail::BenchPhase::None) {
        struct BenchState {
            gentest::detail::FixtureHandle<BenchFixture> fx{gentest::detail::FixtureHandle<BenchFixture>::empty()};
            bool                                         teardown_armed = false;
        };
        static thread_local BenchState bench_state{};

        if (phase == gentest::detail::BenchPhase::Setup) {
            bench_state = BenchState{};
            if (!bench_state.fx.init())
                return;
            bench_state.teardown_armed = true;
            bench_state.fx.ref().setUp();
            return;
        }
        if (phase == gentest::detail::BenchPhase::Teardown) {
            if (bench_state.teardown_armed)
                bench_state.fx.ref().tearDown();
            bench_state = BenchState{};
            return;
        }
        if (phase == gentest::detail::BenchPhase::Call) {
            g_bench_call_count.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        return;
    }
}

constexpr unsigned kJitterTeardownSkipLine = __LINE__ + 1;
void               jitter_teardown_skip(void *) {
    const auto phase = gentest::detail::bench_phase();
    if (phase != gentest::detail::BenchPhase::None) {
        struct BenchState {
            gentest::detail::FixtureHandle<JitterFixture> fx{gentest::detail::FixtureHandle<JitterFixture>::empty()};
            bool                                          teardown_armed = false;
        };
        static thread_local BenchState bench_state{};

        if (phase == gentest::detail::BenchPhase::Setup) {
            bench_state = BenchState{};
            if (!bench_state.fx.init())
                return;
            bench_state.teardown_armed = true;
            bench_state.fx.ref().setUp();
            return;
        }
        if (phase == gentest::detail::BenchPhase::Teardown) {
            if (bench_state.teardown_armed)
                bench_state.fx.ref().tearDown();
            bench_state = BenchState{};
            return;
        }
        if (phase == gentest::detail::BenchPhase::Call) {
            g_jitter_call_count.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        return;
    }
}

constexpr std::string_view kBenchCaseName  = "regressions/measured_local_fixture_teardown_skip/bench";
constexpr std::string_view kJitterCaseName = "regressions/measured_local_fixture_teardown_skip/jitter";

gentest::Case kCases[] = {
    {
        .name             = kBenchCaseName,
        .fn               = &bench_teardown_skip,
        .file             = __FILE__,
        .line             = kBenchTeardownSkipLine,
        .is_benchmark     = true,
        .is_jitter        = false,
        .is_baseline      = false,
        .tags             = {},
        .requirements     = {},
        .skip_reason      = {},
        .should_skip      = false,
        .fixture          = {},
        .fixture_lifetime = gentest::FixtureLifetime::None,
        .suite            = "regressions",
    },
    {
        .name             = kJitterCaseName,
        .fn               = &jitter_teardown_skip,
        .file             = __FILE__,
        .line             = kJitterTeardownSkipLine,
        .is_benchmark     = false,
        .is_jitter        = true,
        .is_baseline      = false,
        .tags             = {},
        .requirements     = {},
        .skip_reason      = {},
        .should_skip      = false,
        .fixture          = {},
        .fixture_lifetime = gentest::FixtureLifetime::None,
        .suite            = "regressions",
    },
};

} // namespace

int main(int argc, char **argv) {
    gentest::detail::register_cases(std::span<const gentest::Case>(kCases));
    const int rc = gentest::run_all_tests(argc, argv);

    if (g_bench_setup_entered.load(std::memory_order_relaxed) && g_bench_teardown_count.load(std::memory_order_relaxed) != 1)
        return 3;
    if (g_jitter_setup_entered.load(std::memory_order_relaxed) && g_jitter_teardown_count.load(std::memory_order_relaxed) != 1)
        return 3;
    if (g_bench_setup_entered.load(std::memory_order_relaxed) && g_bench_call_count.load(std::memory_order_relaxed) == 0)
        return 3;
    if (g_jitter_setup_entered.load(std::memory_order_relaxed) && g_jitter_call_count.load(std::memory_order_relaxed) == 0)
        return 3;
    return rc;
}
