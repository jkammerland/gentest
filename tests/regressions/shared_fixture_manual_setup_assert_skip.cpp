#include "gentest/runner.h"

#include <cstdio>
#include <memory>

namespace {

constexpr std::string_view kFixtureName = "regressions::AssertingSetupFixture";

std::shared_ptr<void> create_fixture(std::string_view, std::string &) { return std::make_shared<int>(1); }

void setup_assert(void *, std::string &) { gentest::asserts::ASSERT_TRUE(false, "manual-setup-assert"); }

void teardown_marker(void *, std::string &) { static_cast<void>(std::fputs("manual-setup-assert-teardown-ran\n", stderr)); }

constexpr unsigned kNoopCaseLine = __LINE__ + 1;
void               noop_case(void *) {}

gentest::Case kCases[] = {
    {
        .name             = "regressions/shared_fixture_manual_setup_assert_skip/member_case",
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
    gentest::detail::register_shared_fixture(gentest::detail::SharedFixtureScope::Global, std::string_view{}, kFixtureName, &create_fixture,
                                             &setup_assert, &teardown_marker);
    gentest::detail::register_cases(std::span<const gentest::Case>(kCases));
    return gentest::run_all_tests(argc, argv);
}
