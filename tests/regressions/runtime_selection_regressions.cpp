#include "gentest/runner.h"

#include <span>

using namespace gentest::asserts;

namespace {

constexpr std::string_view kDeathTag[] = {"death"};

void mixed_summary_test_pass(void *) {}

void mixed_summary_bench_pass(void *) {}

constexpr unsigned kDuplicateFailureOneLine = __LINE__ + 1;
void duplicate_failure_one(void *) { gentest::fail("duplicate-summary-one"); }

constexpr unsigned kDuplicateFailureTwoLine = __LINE__ + 1;
void duplicate_failure_two(void *) { gentest::fail("duplicate-summary-two"); }

void death_case(void *) {}

void bench_table_case(void *) {}

gentest::Case kCases[] = {
    {
        .name             = "regressions/runtime_selection/mixed_summary_test_pass",
        .fn               = &mixed_summary_test_pass,
        .file             = __FILE__,
        .line             = __LINE__,
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
        .name             = "regressions/runtime_selection/mixed_summary_bench_pass",
        .fn               = &mixed_summary_bench_pass,
        .file             = __FILE__,
        .line             = __LINE__,
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
        .name             = "regressions/runtime_selection/duplicate_name",
        .fn               = &duplicate_failure_one,
        .file             = __FILE__,
        .line             = kDuplicateFailureOneLine,
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
        .name             = "regressions/runtime_selection/duplicate_name",
        .fn               = &duplicate_failure_two,
        .file             = __FILE__,
        .line             = kDuplicateFailureTwoLine,
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
        .name             = "regressions/runtime_selection/death_case",
        .fn               = &death_case,
        .file             = __FILE__,
        .line             = __LINE__,
        .is_benchmark     = false,
        .is_jitter        = false,
        .is_baseline      = false,
        .tags             = kDeathTag,
        .requirements     = {},
        .skip_reason      = {},
        .should_skip      = false,
        .fixture          = {},
        .fixture_lifetime = gentest::FixtureLifetime::None,
        .suite            = "regressions",
    },
    {
        .name             = "regressions/runtime_selection/bench_table_case",
        .fn               = &bench_table_case,
        .file             = __FILE__,
        .line             = __LINE__,
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
};

} // namespace

int main(int argc, char **argv) {
    gentest::detail::register_cases(std::span<const gentest::Case>(kCases));
    return gentest::run_all_tests(argc, argv);
}
