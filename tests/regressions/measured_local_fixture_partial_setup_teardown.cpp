#include "gentest/runner.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string_view>

namespace {

std::atomic<int> g_bench_first_teardown_count{0};
std::atomic<int> g_jitter_first_teardown_count{0};

struct BenchFirstFixture : gentest::FixtureSetup, gentest::FixtureTearDown {
    void setUp() override {}
    void tearDown() override { g_bench_first_teardown_count.fetch_add(1, std::memory_order_relaxed); }
};

struct BenchSecondFixture : gentest::FixtureSetup {
    void setUp() override { throw std::runtime_error("bench-second-setup-failed"); }
};

struct JitterFirstFixture : gentest::FixtureSetup, gentest::FixtureTearDown {
    void setUp() override {}
    void tearDown() override { g_jitter_first_teardown_count.fetch_add(1, std::memory_order_relaxed); }
};

struct JitterSecondFixture : gentest::FixtureSetup {
    void setUp() override { throw std::runtime_error("jitter-second-setup-failed"); }
};

void bench_partial_setup_teardown(void *) {
    const auto phase = gentest::detail::bench_phase();
    if (phase != gentest::detail::BenchPhase::None) {
        struct BenchState {
            gentest::detail::FixtureHandle<BenchFirstFixture>  first{gentest::detail::FixtureHandle<BenchFirstFixture>::empty()};
            gentest::detail::FixtureHandle<BenchSecondFixture> second{gentest::detail::FixtureHandle<BenchSecondFixture>::empty()};
            bool                                               first_setup_complete  = false;
            bool                                               ready                 = false;
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
            bench_state.ready                 = true;
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

void jitter_partial_setup_teardown(void *) {
    const auto phase = gentest::detail::bench_phase();
    if (phase != gentest::detail::BenchPhase::None) {
        struct BenchState {
            gentest::detail::FixtureHandle<JitterFirstFixture>  first{gentest::detail::FixtureHandle<JitterFirstFixture>::empty()};
            gentest::detail::FixtureHandle<JitterSecondFixture> second{gentest::detail::FixtureHandle<JitterSecondFixture>::empty()};
            bool                                                first_setup_complete  = false;
            bool                                                ready                 = false;
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
            bench_state.ready                 = true;
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
        .line             = 31,
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
        .line             = 73,
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

bool arg_contains(int argc, char **argv, std::string_view needle) {
    for (int i = 0; i < argc; ++i) {
        if (std::strstr(argv[i], needle.data()) != nullptr) {
            return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char **argv) {
    gentest::detail::register_cases(std::span<const gentest::Case>(kCases));
    const int rc = gentest::run_all_tests(argc, argv);

    const bool run_bench_case  = arg_contains(argc, argv, kBenchCaseName);
    const bool run_jitter_case = arg_contains(argc, argv, kJitterCaseName);

    if (run_bench_case && g_bench_first_teardown_count.load(std::memory_order_relaxed) != 1) {
        return 3;
    }
    if (run_jitter_case && g_jitter_first_teardown_count.load(std::memory_order_relaxed) != 1) {
        return 3;
    }
    return rc;
}
