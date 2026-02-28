#include "gentest/runner.h"

#include <stdexcept>

using namespace gentest::asserts;

namespace {

bool in_bench_call_phase() {
    return gentest::detail::bench_phase() == gentest::detail::BenchPhase::Call;
}

void bench_assert_should_fail(void*) {
    if (!in_bench_call_phase())
        return;
    EXPECT_TRUE(false, "intentional benchmark assertion failure");
}

void bench_std_exception_should_fail(void*) {
    if (!in_bench_call_phase())
        return;
    throw std::runtime_error("intentional benchmark std::exception failure");
}

void bench_fail_should_fail(void*) {
    if (!in_bench_call_phase())
        return;
    gentest::fail("intentional benchmark gentest::failure");
}

void bench_skip_should_fail(void*) {
    if (!in_bench_call_phase())
        return;
    gentest::skip("intentional benchmark skip in call phase");
}

void jitter_assert_should_fail(void*) {
    if (!in_bench_call_phase())
        return;
    EXPECT_TRUE(false, "intentional jitter assertion failure");
}

void jitter_std_exception_should_fail(void*) {
    if (!in_bench_call_phase())
        return;
    throw std::runtime_error("intentional jitter std::exception failure");
}

void jitter_fail_should_fail(void*) {
    if (!in_bench_call_phase())
        return;
    gentest::fail("intentional jitter gentest::failure");
}

void jitter_skip_should_fail(void*) {
    if (!in_bench_call_phase())
        return;
    gentest::skip("intentional jitter skip in call phase");
}

gentest::Case kCases[] = {
    {
        .name = "regressions/bench_assert_should_fail",
        .fn = &bench_assert_should_fail,
        .file = __FILE__,
        .line = 8,
        .is_benchmark = true,
        .is_jitter = false,
        .is_baseline = false,
        .tags = {},
        .requirements = {},
        .skip_reason = {},
        .should_skip = false,
        .fixture = {},
        .fixture_lifetime = gentest::FixtureLifetime::None,
        .suite = "regressions",
    },
    {
        .name = "regressions/bench_std_exception_should_fail",
        .fn = &bench_std_exception_should_fail,
        .file = __FILE__,
        .line = 10,
        .is_benchmark = true,
        .is_jitter = false,
        .is_baseline = false,
        .tags = {},
        .requirements = {},
        .skip_reason = {},
        .should_skip = false,
        .fixture = {},
        .fixture_lifetime = gentest::FixtureLifetime::None,
        .suite = "regressions",
    },
    {
        .name = "regressions/bench_fail_should_fail",
        .fn = &bench_fail_should_fail,
        .file = __FILE__,
        .line = 14,
        .is_benchmark = true,
        .is_jitter = false,
        .is_baseline = false,
        .tags = {},
        .requirements = {},
        .skip_reason = {},
        .should_skip = false,
        .fixture = {},
        .fixture_lifetime = gentest::FixtureLifetime::None,
        .suite = "regressions",
    },
    {
        .name = "regressions/bench_skip_should_fail",
        .fn = &bench_skip_should_fail,
        .file = __FILE__,
        .line = 18,
        .is_benchmark = true,
        .is_jitter = false,
        .is_baseline = false,
        .tags = {},
        .requirements = {},
        .skip_reason = {},
        .should_skip = false,
        .fixture = {},
        .fixture_lifetime = gentest::FixtureLifetime::None,
        .suite = "regressions",
    },
    {
        .name = "regressions/jitter_assert_should_fail",
        .fn = &jitter_assert_should_fail,
        .file = __FILE__,
        .line = 12,
        .is_benchmark = false,
        .is_jitter = true,
        .is_baseline = false,
        .tags = {},
        .requirements = {},
        .skip_reason = {},
        .should_skip = false,
        .fixture = {},
        .fixture_lifetime = gentest::FixtureLifetime::None,
        .suite = "regressions",
    },
    {
        .name = "regressions/jitter_std_exception_should_fail",
        .fn = &jitter_std_exception_should_fail,
        .file = __FILE__,
        .line = 26,
        .is_benchmark = false,
        .is_jitter = true,
        .is_baseline = false,
        .tags = {},
        .requirements = {},
        .skip_reason = {},
        .should_skip = false,
        .fixture = {},
        .fixture_lifetime = gentest::FixtureLifetime::None,
        .suite = "regressions",
    },
    {
        .name = "regressions/jitter_fail_should_fail",
        .fn = &jitter_fail_should_fail,
        .file = __FILE__,
        .line = 30,
        .is_benchmark = false,
        .is_jitter = true,
        .is_baseline = false,
        .tags = {},
        .requirements = {},
        .skip_reason = {},
        .should_skip = false,
        .fixture = {},
        .fixture_lifetime = gentest::FixtureLifetime::None,
        .suite = "regressions",
    },
    {
        .name = "regressions/jitter_skip_should_fail",
        .fn = &jitter_skip_should_fail,
        .file = __FILE__,
        .line = 34,
        .is_benchmark = false,
        .is_jitter = true,
        .is_baseline = false,
        .tags = {},
        .requirements = {},
        .skip_reason = {},
        .should_skip = false,
        .fixture = {},
        .fixture_lifetime = gentest::FixtureLifetime::None,
        .suite = "regressions",
    },
};

} // namespace

int main(int argc, char** argv) {
    gentest::detail::register_cases(std::span<const gentest::Case>(kCases));
    return gentest::run_all_tests(argc, argv);
}
