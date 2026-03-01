#include "gentest/runner.h"

#include <memory>

namespace {

constexpr std::string_view kFixtureName = "regressions::LateRegisteredFixture";

std::shared_ptr<void> create_fixture(std::string_view, std::string &) { return std::make_shared<int>(1); }

void late_register_case(void *) {
    gentest::detail::register_shared_fixture({
        .fixture_name = kFixtureName,
        .suite        = std::string_view{},
        .scope        = gentest::detail::SharedFixtureScope::Global,
        .create       = &create_fixture,
        .setup        = nullptr,
        .teardown     = nullptr,
    });

    std::string error;
    auto        fixture =
        gentest::detail::get_shared_fixture(gentest::detail::SharedFixtureScope::Global, std::string_view{}, kFixtureName, error);
    gentest::expect(!fixture, "runtime fixture registration should be rejected while a run is active");
    gentest::expect(error.find("cannot be registered while a test run is active") != std::string::npos,
                    "runtime fixture registration rejection reason mismatch");
}

gentest::Case kCases[] = {
    {
        .name             = "regressions/shared_fixture_runtime_registration_during_run/late_register",
        .fn               = &late_register_case,
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
    gentest::detail::register_cases(std::span<const gentest::Case>(kCases));
    return gentest::run_all_tests(argc, argv);
}
