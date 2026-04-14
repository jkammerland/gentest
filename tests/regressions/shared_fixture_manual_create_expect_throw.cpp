#include "gentest/runner.h"

#include <memory>
#include <stdexcept>

namespace {

constexpr std::string_view kFixtureName = "regressions::ExpectThrowCreateFixture";

std::shared_ptr<void> create_expect_throw(std::string_view, std::string &) {
    gentest::asserts::EXPECT_TRUE(false, "manual-create-expect-before-throw");
    throw std::runtime_error("manual-create-throw-after-expect");
}

constexpr unsigned kNoopCaseLine = __LINE__ + 1;
void               noop_case(void *) {}

gentest::Case kCases[] = {
    {
        .name             = "regressions/shared_fixture_manual_create_expect_throw/member_case",
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
    gentest::detail::register_shared_fixture(gentest::detail::SharedFixtureScope::Global, std::string_view{}, kFixtureName,
                                             &create_expect_throw, nullptr, nullptr);
    gentest::detail::register_cases(std::span<const gentest::Case>(kCases));
    return gentest::run_all_tests(argc, argv);
}
