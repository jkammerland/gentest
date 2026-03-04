#include "gentest/runner.h"

#include <stdexcept>

using namespace gentest::asserts;

namespace {

bool in_bench_call_phase() {
    return gentest::detail::bench_phase() == gentest::detail::BenchPhase::Call;
}

constexpr unsigned kBenchAssertShouldFailLine = __LINE__ + 1;
void bench_assert_should_fail(void*) {
    if (!in_bench_call_phase())
        return;
    EXPECT_TRUE(false, "intentional benchmark assertion failure");
}

constexpr unsigned kBenchStdExceptionShouldFailLine = __LINE__ + 1;
void bench_std_exception_should_fail(void*) {
    if (!in_bench_call_phase())
        return;
    throw std::runtime_error("intentional benchmark std::exception failure");
}

constexpr unsigned kBenchFailShouldFailLine = __LINE__ + 1;
void bench_fail_should_fail(void*) {
    if (!in_bench_call_phase())
        return;
    gentest::fail("intentional benchmark gentest::failure");
}

constexpr unsigned kBenchSkipShouldFailLine = __LINE__ + 1;
void bench_skip_should_fail(void*) {
    if (!in_bench_call_phase())
        return;
    gentest::skip("intentional benchmark skip in call phase");
}

constexpr unsigned kBenchSetupSkipShouldNotFailLine = __LINE__ + 1;
void bench_setup_skip_should_not_fail(void*) {
    if (gentest::detail::bench_phase() != gentest::detail::BenchPhase::Setup)
        return;
    gentest::skip("intentional benchmark setup skip");
}

constexpr unsigned kJitterAssertShouldFailLine = __LINE__ + 1;
void jitter_assert_should_fail(void*) {
    if (!in_bench_call_phase())
        return;
    EXPECT_TRUE(false, "intentional jitter assertion failure");
}

constexpr unsigned kJitterStdExceptionShouldFailLine = __LINE__ + 1;
void jitter_std_exception_should_fail(void*) {
    if (!in_bench_call_phase())
        return;
    throw std::runtime_error("intentional jitter std::exception failure");
}

constexpr unsigned kJitterFailShouldFailLine = __LINE__ + 1;
void jitter_fail_should_fail(void*) {
    if (!in_bench_call_phase())
        return;
    gentest::fail("intentional jitter gentest::failure");
}

constexpr unsigned kJitterSkipShouldFailLine = __LINE__ + 1;
void jitter_skip_should_fail(void*) {
    if (!in_bench_call_phase())
        return;
    gentest::skip("intentional jitter skip in call phase");
}

constexpr unsigned kJitterSetupSkipShouldNotFailLine = __LINE__ + 1;
void jitter_setup_skip_should_not_fail(void*) {
    if (gentest::detail::bench_phase() != gentest::detail::BenchPhase::Setup)
        return;
    gentest::skip("intentional jitter setup skip");
}

gentest::Case kCases[] = {
    {
        .name = "regressions/bench_assert_should_fail",
        .fn = &bench_assert_should_fail,
        .file = __FILE__,
        .line = kBenchAssertShouldFailLine,
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
        .line = kBenchStdExceptionShouldFailLine,
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
        .line = kBenchFailShouldFailLine,
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
        .line = kBenchSkipShouldFailLine,
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
        .name = "regressions/bench_setup_skip_should_not_fail",
        .fn = &bench_setup_skip_should_not_fail,
        .file = __FILE__,
        .line = kBenchSetupSkipShouldNotFailLine,
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
        .line = kJitterAssertShouldFailLine,
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
        .line = kJitterStdExceptionShouldFailLine,
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
        .line = kJitterFailShouldFailLine,
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
        .line = kJitterSkipShouldFailLine,
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
        .name = "regressions/jitter_setup_skip_should_not_fail",
        .fn = &jitter_setup_skip_should_not_fail,
        .file = __FILE__,
        .line = kJitterSetupSkipShouldNotFailLine,
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
