#include "gentest/runner.h"

#include <memory>

namespace {

constexpr std::string_view kPrimaryFixtureName   = "regressions::ReentryFixturePrimary";
constexpr std::string_view kSecondaryFixtureName = "regressions::ReentryFixtureSecondary";

std::shared_ptr<void> create_primary_fixture(std::string_view, std::string &) { return std::make_shared<int>(42); }

void setup_primary_fixture(void *, std::string &error) {
    std::string inner_error;
    auto inner = gentest::detail::get_shared_fixture(gentest::detail::SharedFixtureScope::Global, std::string_view{}, kPrimaryFixtureName,
                                                     inner_error);
    if (inner) {
        error = "fixture should not be visible during setup";
        return;
    }
    if (inner_error != "fixture initialization in progress") {
        error = "unexpected reentry status: " + inner_error;
    }
}

void teardown_primary_fixture(void *, std::string &error) {
    std::string inner_error;
    auto primary = gentest::detail::get_shared_fixture(gentest::detail::SharedFixtureScope::Global, std::string_view{}, kPrimaryFixtureName,
                                                       inner_error);
    if (!primary) {
        error = "primary fixture should stay accessible during teardown: " + inner_error;
        return;
    }

    std::string secondary_error;
    auto secondary = gentest::detail::get_shared_fixture(gentest::detail::SharedFixtureScope::Global, std::string_view{},
                                                         kSecondaryFixtureName, secondary_error);
    if (secondary) {
        error = "secondary fixture should not be lazily recreated during teardown";
    }
}

std::shared_ptr<void> create_secondary_fixture(std::string_view, std::string &) { return std::make_shared<int>(7); }

void teardown_secondary_fixture(void *, std::string &error) {
    std::string inner_error;
    auto inner = gentest::detail::get_shared_fixture(gentest::detail::SharedFixtureScope::Global, std::string_view{}, kPrimaryFixtureName,
                                                     inner_error);
    if (!inner) {
        error = "primary fixture should stay accessible while secondary teardown runs: " + inner_error;
    }
}

void smoke_test(void *) {}

gentest::Case kCases[] = {
    {
        .name             = "regressions/shared_fixture_reentry_smoke",
        .fn               = &smoke_test,
        .file             = __FILE__,
        .line             = 20,
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
    gentest::detail::SharedFixtureRegistration primary_registration{
        .fixture_name = kPrimaryFixtureName,
        .suite        = std::string_view{},
        .scope        = gentest::detail::SharedFixtureScope::Global,
        .create       = &create_primary_fixture,
        .setup        = &setup_primary_fixture,
        .teardown     = &teardown_primary_fixture,
    };
    gentest::detail::register_shared_fixture(primary_registration);

    gentest::detail::SharedFixtureRegistration secondary_registration{
        .fixture_name = kSecondaryFixtureName,
        .suite        = std::string_view{},
        .scope        = gentest::detail::SharedFixtureScope::Global,
        .create       = &create_secondary_fixture,
        .setup        = nullptr,
        .teardown     = &teardown_secondary_fixture,
    };
    gentest::detail::register_shared_fixture(secondary_registration);

    gentest::detail::register_cases(std::span<const gentest::Case>(kCases));
    return gentest::run_all_tests(argc, argv);
}
