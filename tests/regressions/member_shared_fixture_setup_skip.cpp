#include "gentest/runner.h"

#include <memory>

namespace {

constexpr std::string_view kSuiteFixture  = "regressions::NullMemberSuiteFixture";
constexpr std::string_view kGlobalFixture = "regressions::NullMemberGlobalFixture";

std::shared_ptr<void> create_null_fixture(std::string_view, std::string &) { return {}; }

void noop_case(void *) {}

gentest::Case kCases[] = {
    {
        .name             = "regressions/member_shared_setup_skip/suite_member",
        .fn               = &noop_case,
        .file             = __FILE__,
        .line             = 12,
        .is_benchmark     = false,
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
        .name             = "regressions/member_shared_setup_skip/global_member",
        .fn               = &noop_case,
        .file             = __FILE__,
        .line             = 13,
        .is_benchmark     = false,
        .is_jitter        = false,
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
    gentest::detail::register_shared_fixture({
        .fixture_name = kSuiteFixture,
        .suite        = "regressions",
        .scope        = gentest::detail::SharedFixtureScope::Suite,
        .create       = &create_null_fixture,
        .setup        = nullptr,
        .teardown     = nullptr,
    });

    gentest::detail::register_shared_fixture({
        .fixture_name = kGlobalFixture,
        .suite        = std::string_view{},
        .scope        = gentest::detail::SharedFixtureScope::Global,
        .create       = &create_null_fixture,
        .setup        = nullptr,
        .teardown     = nullptr,
    });

    gentest::detail::register_cases(std::span<const gentest::Case>(kCases));
    return gentest::run_all_tests(argc, argv);
}
