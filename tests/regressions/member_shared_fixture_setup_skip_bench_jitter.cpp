#include "gentest/runner.h"

namespace {

constexpr std::string_view kSuiteFixture  = "regressions::MissingBenchSuiteFixture";
constexpr std::string_view kGlobalFixture = "regressions::MissingJitterGlobalFixture";

void noop_case(void *) {}

gentest::Case kCases[] = {
    {
        .name             = "regressions/member_shared_setup_skip_measured/bench_member",
        .fn               = &noop_case,
        .file             = __FILE__,
        .line             = 11,
        .is_benchmark     = true,
        .is_jitter        = false,
        .is_baseline      = false,
        .tags             = {},
        .requirements     = {},
        .skip_reason      = {},
        .should_skip      = false,
        .fixture          = kSuiteFixture,
        .fixture_lifetime = gentest::FixtureLifetime::MemberSuite,
        .suite            = "regressions",
    },
    {
        .name             = "regressions/member_shared_setup_skip_measured/jitter_member",
        .fn               = &noop_case,
        .file             = __FILE__,
        .line             = 12,
        .is_benchmark     = false,
        .is_jitter        = true,
        .is_baseline      = false,
        .tags             = {},
        .requirements     = {},
        .skip_reason      = {},
        .should_skip      = false,
        .fixture          = kGlobalFixture,
        .fixture_lifetime = gentest::FixtureLifetime::MemberGlobal,
        .suite            = "regressions",
    },
};

} // namespace

int main(int argc, char **argv) {
    gentest::detail::register_cases(std::span<const gentest::Case>(kCases));
    return gentest::run_all_tests(argc, argv);
}
