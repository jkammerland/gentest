#include "gentest/runner.h"

#include <atomic>
#include <stdexcept>
#include <string_view>

namespace {

std::atomic<bool> g_bench_setup_entered{false};
std::atomic<int>  g_bench_teardown_count{0};
std::atomic<bool> g_jitter_setup_entered{false};
std::atomic<int>  g_jitter_teardown_count{0};

struct BenchDualFailFixture : gentest::FixtureSetup, gentest::FixtureTearDown {
    void setUp() override { g_bench_setup_entered.store(true, std::memory_order_relaxed); }
    void tearDown() override {
        g_bench_teardown_count.fetch_add(1, std::memory_order_relaxed);
        throw std::runtime_error("bench-teardown-phase-failure-marker");
    }
};

struct JitterDualFailFixture : gentest::FixtureSetup, gentest::FixtureTearDown {
    void setUp() override { g_jitter_setup_entered.store(true, std::memory_order_relaxed); }
    void tearDown() override {
        g_jitter_teardown_count.fetch_add(1, std::memory_order_relaxed);
        throw std::runtime_error("jitter-teardown-phase-failure-marker");
    }
};

void bench_call_and_teardown_fail(void *) {
    const auto phase = gentest::detail::bench_phase();
    if (phase != gentest::detail::BenchPhase::None) {
        struct BenchState {
            gentest::detail::FixtureHandle<BenchDualFailFixture> fx{gentest::detail::FixtureHandle<BenchDualFailFixture>::empty()};
            bool                                                  teardown_armed = false;
            bool                                                  ready          = false;
        };
        static thread_local BenchState bench_state{};
        if (phase == gentest::detail::BenchPhase::Setup) {
            bench_state = BenchState{};
            if (!bench_state.fx.init())
                return;
            bench_state.teardown_armed = true;
            bench_state.fx.ref().setUp();
            bench_state.ready = true;
            return;
        }
        if (phase == gentest::detail::BenchPhase::Teardown) {
            if (bench_state.teardown_armed)
                bench_state.fx.ref().tearDown();
            bench_state = BenchState{};
            return;
        }
        if (phase == gentest::detail::BenchPhase::Call) {
            if (!bench_state.ready)
                return;
            gentest::asserts::EXPECT_TRUE(false, "bench-call-phase-failure-marker");
            return;
        }
        return;
    }
}

void jitter_call_and_teardown_fail(void *) {
    const auto phase = gentest::detail::bench_phase();
    if (phase != gentest::detail::BenchPhase::None) {
        struct BenchState {
            gentest::detail::FixtureHandle<JitterDualFailFixture> fx{gentest::detail::FixtureHandle<JitterDualFailFixture>::empty()};
            bool                                                   teardown_armed = false;
            bool                                                   ready          = false;
        };
        static thread_local BenchState bench_state{};
        if (phase == gentest::detail::BenchPhase::Setup) {
            bench_state = BenchState{};
            if (!bench_state.fx.init())
                return;
            bench_state.teardown_armed = true;
            bench_state.fx.ref().setUp();
            bench_state.ready = true;
            return;
        }
        if (phase == gentest::detail::BenchPhase::Teardown) {
            if (bench_state.teardown_armed)
                bench_state.fx.ref().tearDown();
            bench_state = BenchState{};
            return;
        }
        if (phase == gentest::detail::BenchPhase::Call) {
            if (!bench_state.ready)
                return;
            gentest::asserts::EXPECT_TRUE(false, "jitter-call-phase-failure-marker");
            return;
        }
        return;
    }
}

constexpr std::string_view kBenchCaseName = "regressions/measured_local_fixture_call_teardown_dualfail/bench";
constexpr std::string_view kJitterCaseName = "regressions/measured_local_fixture_call_teardown_dualfail/jitter";

gentest::Case kCases[] = {
    {
        .name             = kBenchCaseName,
        .fn               = &bench_call_and_teardown_fail,
        .file             = __FILE__,
        .line             = 8,
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
        .fn               = &jitter_call_and_teardown_fail,
        .file             = __FILE__,
        .line             = 57,
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
    return rc;
}
