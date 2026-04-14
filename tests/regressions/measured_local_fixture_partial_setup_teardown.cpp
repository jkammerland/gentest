#include "gentest/detail/fixture_runtime.h"
#include "gentest/detail/registry_runtime.h"
#include "gentest/runner.h"

#include <atomic>
#include <cstdlib>
#include <stdexcept>
#include <string_view>

namespace {

std::atomic<bool> g_bench_first_setup_entered{false};
std::atomic<int>  g_bench_first_teardown_count{0};
std::atomic<bool> g_jitter_first_setup_entered{false};
std::atomic<int>  g_jitter_first_teardown_count{0};

struct BenchFirstFixture : gentest::FixtureSetup, gentest::FixtureTearDown {
    void setUp() override { g_bench_first_setup_entered.store(true, std::memory_order_relaxed); }
    void tearDown() override { g_bench_first_teardown_count.fetch_add(1, std::memory_order_relaxed); }
};

struct BenchSecondFixture : gentest::FixtureSetup {
    void setUp() override { throw std::runtime_error("bench-second-setup-failed"); }
};

struct JitterFirstFixture : gentest::FixtureSetup, gentest::FixtureTearDown {
    void setUp() override { g_jitter_first_setup_entered.store(true, std::memory_order_relaxed); }
    void tearDown() override { g_jitter_first_teardown_count.fetch_add(1, std::memory_order_relaxed); }
};

struct JitterSecondFixture : gentest::FixtureSetup {
    void setUp() override { throw std::runtime_error("jitter-second-setup-failed"); }
};

constexpr unsigned kBenchPartialSetupTeardownLine = __LINE__ + 1;
void               bench_partial_setup_teardown(void *) {
    const auto phase = gentest::detail::bench_phase();
    if (phase != gentest::detail::BenchPhase::None) {
        struct BenchState {
            gentest::detail::FixtureHandle<BenchFirstFixture>  first{gentest::detail::FixtureHandle<BenchFirstFixture>::empty()};
            gentest::detail::FixtureHandle<BenchSecondFixture> second{gentest::detail::FixtureHandle<BenchSecondFixture>::empty()};
            bool                                               first_setup_complete = false;
            bool                                               ready                = false;
        };
        static thread_local BenchState bench_state{};
        if (phase == gentest::detail::BenchPhase::Setup) {
            bench_state = BenchState{};
            if (!bench_state.first.init())
                return;
            if (!bench_state.second.init())
                return;
            bench_state.first.ref().setUp();
            bench_state.first_setup_complete = true;
            bench_state.second.ref().setUp();
            bench_state.ready = true;
            return;
        }
        if (phase == gentest::detail::BenchPhase::Teardown) {
            if (bench_state.first_setup_complete) {
                bench_state.first.ref().tearDown();
            }
            bench_state = BenchState{};
            return;
        }
        if (phase == gentest::detail::BenchPhase::Call) {
            if (!bench_state.ready)
                return;
            return;
        }
        return;
    }
}

constexpr unsigned kJitterPartialSetupTeardownLine = __LINE__ + 1;
void               jitter_partial_setup_teardown(void *) {
    const auto phase = gentest::detail::bench_phase();
    if (phase != gentest::detail::BenchPhase::None) {
        struct BenchState {
            gentest::detail::FixtureHandle<JitterFirstFixture>  first{gentest::detail::FixtureHandle<JitterFirstFixture>::empty()};
            gentest::detail::FixtureHandle<JitterSecondFixture> second{gentest::detail::FixtureHandle<JitterSecondFixture>::empty()};
            bool                                                first_setup_complete = false;
            bool                                                ready                = false;
        };
        static thread_local BenchState bench_state{};
        if (phase == gentest::detail::BenchPhase::Setup) {
            bench_state = BenchState{};
            if (!bench_state.first.init())
                return;
            if (!bench_state.second.init())
                return;
            bench_state.first.ref().setUp();
            bench_state.first_setup_complete = true;
            bench_state.second.ref().setUp();
            bench_state.ready = true;
            return;
        }
        if (phase == gentest::detail::BenchPhase::Teardown) {
            if (bench_state.first_setup_complete) {
                bench_state.first.ref().tearDown();
            }
            bench_state = BenchState{};
            return;
        }
        if (phase == gentest::detail::BenchPhase::Call) {
            if (!bench_state.ready)
                return;
            return;
        }
        return;
    }
}

constexpr std::string_view kBenchCaseName  = "regressions/measured_local_fixture_partial_setup_teardown/bench";
constexpr std::string_view kJitterCaseName = "regressions/measured_local_fixture_partial_setup_teardown/jitter";

gentest::Case kCases[] = {
    {
        .name             = kBenchCaseName,
        .fn               = &bench_partial_setup_teardown,
        .file             = __FILE__,
        .line             = kBenchPartialSetupTeardownLine,
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
        .fn               = &jitter_partial_setup_teardown,
        .file             = __FILE__,
        .line             = kJitterPartialSetupTeardownLine,
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

    if (g_bench_first_setup_entered.load(std::memory_order_relaxed) && g_bench_first_teardown_count.load(std::memory_order_relaxed) != 1) {
        return 3;
    }
    if (g_jitter_first_setup_entered.load(std::memory_order_relaxed) &&
        g_jitter_first_teardown_count.load(std::memory_order_relaxed) != 1) {
        return 3;
    }
    return rc;
}
