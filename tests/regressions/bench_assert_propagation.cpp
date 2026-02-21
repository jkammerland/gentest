#include "gentest/runner.h"

using namespace gentest::asserts;

namespace {

void bench_assert_should_fail(void*) {
    EXPECT_TRUE(false, "intentional benchmark assertion failure");
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
};

} // namespace

int main(int argc, char** argv) {
    gentest::detail::register_cases(std::span<const gentest::Case>(kCases));
    return gentest::run_all_tests(argc, argv);
}
