#include "gentest/runner.h"

#include <stdexcept>

using namespace gentest::asserts;

namespace {

void test_fail(void *) { EXPECT_TRUE(false, "orchestrator-fail-fast-test-failure"); }

void bench_should_not_run(void *) {
    if (gentest::detail::bench_phase() == gentest::detail::BenchPhase::Call)
        throw std::runtime_error("orchestrator-fail-fast-bench-ran");
}

void jitter_should_not_run(void *) {
    if (gentest::detail::bench_phase() == gentest::detail::BenchPhase::Call)
        throw std::runtime_error("orchestrator-fail-fast-jitter-ran");
}

gentest::Case kCases[] = {
    {
        .name             = "regressions/orchestrator_fail_fast_blocks_measured/test_fail",
        .fn               = &test_fail,
        .file             = __FILE__,
        .line             = 11,
        .is_benchmark     = false,
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
        .name             = "regressions/orchestrator_fail_fast_blocks_measured/bench_should_not_run",
        .fn               = &bench_should_not_run,
        .file             = __FILE__,
        .line             = 14,
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
        .name             = "regressions/orchestrator_fail_fast_blocks_measured/jitter_should_not_run",
        .fn               = &jitter_should_not_run,
        .file             = __FILE__,
        .line             = 19,
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
    return gentest::run_all_tests(argc, argv);
}
