#include "gentest/runner.h"

#include <memory>
#include <stdexcept>

namespace {

constexpr std::string_view kFixtureName = "regressions::ExpectThrowTeardownFixture";

std::shared_ptr<void> create_fixture(std::string_view, std::string &) { return std::make_shared<int>(1); }

void teardown_expect_throw(void *, std::string &) {
    gentest::asserts::EXPECT_TRUE(false, "manual-teardown-expect-before-throw");
    throw std::runtime_error("manual-teardown-throw-after-expect");
}

constexpr unsigned kSmokeLine = __LINE__ + 1;
void               smoke(void *) {}

gentest::Case kCases[] = {
    {
        .name             = "regressions/shared_fixture_manual_teardown_expect_throw_exit/smoke",
        .fn               = &smoke,
        .file             = __FILE__,
        .line             = kSmokeLine,
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
};

} // namespace

int main(int argc, char **argv) {
    gentest::detail::register_shared_fixture({
        .fixture_name = kFixtureName,
        .suite        = std::string_view{},
        .scope        = gentest::detail::SharedFixtureScope::Global,
        .create       = &create_fixture,
        .setup        = nullptr,
        .teardown     = &teardown_expect_throw,
    });
    gentest::detail::register_cases(std::span<const gentest::Case>(kCases));
    return gentest::run_all_tests(argc, argv);
}
