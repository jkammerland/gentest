#include "gentest/runner.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string_view>

namespace {

std::atomic<int> g_bench_teardown_count{0};
std::atomic<int> g_jitter_teardown_count{0};

struct BenchFixture : gentest::FixtureSetup, gentest::FixtureTearDown {
    void setUp() override { throw std::runtime_error("bench-setup-throws"); }
    void tearDown() override { g_bench_teardown_count.fetch_add(1, std::memory_order_relaxed); }
};

struct JitterFixture : gentest::FixtureSetup, gentest::FixtureTearDown {
    void setUp() override { throw std::runtime_error("jitter-setup-throws"); }
    void tearDown() override { g_jitter_teardown_count.fetch_add(1, std::memory_order_relaxed); }
};

void bench_setup_throw_teardown_armed(void *) {
    const auto phase = gentest::detail::bench_phase();
    if (phase != gentest::detail::BenchPhase::None) {
        struct BenchState {
            gentest::detail::FixtureHandle<BenchFixture> fx{gentest::detail::FixtureHandle<BenchFixture>::empty()};
            bool                                         teardown_armed = false;
            bool                                         ready          = false;
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
            return;
        }
        return;
    }
}

void jitter_setup_throw_teardown_armed(void *) {
    const auto phase = gentest::detail::bench_phase();
    if (phase != gentest::detail::BenchPhase::None) {
        struct BenchState {
            gentest::detail::FixtureHandle<JitterFixture> fx{gentest::detail::FixtureHandle<JitterFixture>::empty()};
            bool                                          teardown_armed = false;
            bool                                          ready          = false;
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
            return;
        }
        return;
    }
}

constexpr std::string_view kBenchCaseName  = "regressions/measured_local_fixture_setup_throw_teardown_armed/bench";
constexpr std::string_view kJitterCaseName = "regressions/measured_local_fixture_setup_throw_teardown_armed/jitter";

gentest::Case kCases[] = {
    {
        .name             = kBenchCaseName,
        .fn               = &bench_setup_throw_teardown_armed,
        .file             = __FILE__,
        .line             = 24,
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
        .fn               = &jitter_setup_throw_teardown_armed,
        .file             = __FILE__,
        .line             = 58,
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
        if (std::strstr(argv[i], needle.data()) != nullptr)
            return true;
    }
    return false;
}

} // namespace

int main(int argc, char **argv) {
    gentest::detail::register_cases(std::span<const gentest::Case>(kCases));
    const int rc = gentest::run_all_tests(argc, argv);

    const bool run_bench_case  = arg_contains(argc, argv, kBenchCaseName);
    const bool run_jitter_case = arg_contains(argc, argv, kJitterCaseName);

    if (run_bench_case && g_bench_teardown_count.load(std::memory_order_relaxed) != 1)
        return 3;
    if (run_jitter_case && g_jitter_teardown_count.load(std::memory_order_relaxed) != 1)
        return 3;
    return rc;
}
