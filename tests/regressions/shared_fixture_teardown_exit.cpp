#include "gentest/runner.h"

#include <memory>

namespace {

constexpr std::string_view kFixtureName = "regressions::TeardownFailureFixture";

std::shared_ptr<void> create_fixture(std::string_view, std::string &) { return std::make_shared<int>(1); }

void setup_fixture(void *, std::string &) {}

void teardown_fixture(void *, std::string &error) { error = "intentional shared fixture teardown failure"; }

void smoke_test(void *) {}

gentest::Case kCases[] = {
    {
        .name             = "regressions/shared_fixture_teardown_failure_exit",
        .fn               = &smoke_test,
        .file             = __FILE__,
        .line             = 15,
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
    gentest::detail::SharedFixtureRegistration registration{
        .fixture_name = kFixtureName,
        .suite        = std::string_view{},
        .scope        = gentest::detail::SharedFixtureScope::Global,
        .create       = &create_fixture,
        .setup        = &setup_fixture,
        .teardown     = &teardown_fixture,
    };
    gentest::detail::register_shared_fixture(registration);
    gentest::detail::register_cases(std::span<const gentest::Case>(kCases));
    return gentest::run_all_tests(argc, argv);
}
