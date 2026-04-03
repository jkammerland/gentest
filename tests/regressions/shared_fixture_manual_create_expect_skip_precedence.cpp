#include "gentest/runner.h"

namespace {

constexpr std::string_view kFixtureName = "regressions::ExpectSkipCreateFixture";

std::shared_ptr<void> create_expect_skip(std::string_view, std::string &) {
    gentest::asserts::EXPECT_TRUE(false, "manual-create-expect-before-skip");
    gentest::skip("manual-create-skip-after-failure");
    return {};
}

constexpr unsigned kNoopCaseLine = __LINE__ + 1;
void               noop_case(void *) {}

gentest::Case kCases[] = {
    {
        .name             = "regressions/shared_fixture_manual_create_expect_skip_precedence/member_case",
        .fn               = &noop_case,
        .file             = __FILE__,
        .line             = kNoopCaseLine,
        .is_benchmark     = false,
        .is_jitter        = false,
        .is_baseline      = false,
        .tags             = {},
        .requirements     = {},
        .skip_reason      = {},
        .should_skip      = false,
        .fixture          = kFixtureName,
        .fixture_lifetime = gentest::FixtureLifetime::MemberGlobal,
        .suite            = "regressions",
    },
};

} // namespace

int main(int argc, char **argv) {
    gentest::detail::register_shared_fixture({
        .fixture_name = kFixtureName,
        .suite        = std::string_view{},
        .scope        = gentest::detail::SharedFixtureScope::Global,
        .create       = &create_expect_skip,
        .setup        = nullptr,
        .teardown     = nullptr,
    });
    gentest::detail::register_cases(std::span<const gentest::Case>(kCases));
    return gentest::run_all_tests(argc, argv);
}
