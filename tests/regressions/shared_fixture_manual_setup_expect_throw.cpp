#include "gentest/detail/fixture_runtime.h"
#include "gentest/detail/registry_runtime.h"
#include "gentest/runner.h"

#include <memory>
#include <stdexcept>

namespace {

constexpr std::string_view kFixtureName = "regressions::ExpectThrowSetupFixture";

std::shared_ptr<void> create_fixture(std::string_view, std::string &) { return std::make_shared<int>(1); }

void setup_expect_throw(void *, std::string &) {
    gentest::asserts::EXPECT_TRUE(false, "manual-setup-expect-before-throw");
    throw std::runtime_error("manual-setup-throw-after-expect");
}

constexpr unsigned kNoopCaseLine = __LINE__ + 1;
void               noop_case(void *) {}

gentest::Case kCases[] = {
    {
        .name             = "regressions/shared_fixture_manual_setup_expect_throw/member_case",
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
                                             &setup_expect_throw, nullptr);
    gentest::detail::register_cases(std::span<const gentest::Case>(kCases));
    return gentest::run_all_tests(argc, argv);
}
