#include "gentest/runner.h"

#include <memory>

namespace {

constexpr std::string_view kFixtureName = "regressions::ReentryFixture";

std::shared_ptr<void> create_fixture(std::string_view, std::string&) { return std::make_shared<int>(42); }

void setup_fixture(void*, std::string& error) {
    std::string inner_error;
    auto inner = gentest::detail::get_shared_fixture(gentest::detail::SharedFixtureScope::Global, std::string_view{}, kFixtureName, inner_error);
    if (inner) {
        error = "fixture should not be visible during setup";
        return;
    }
    if (inner_error != "fixture initialization in progress") {
        error = "unexpected reentry status: " + inner_error;
    }
}

void teardown_fixture(void*, std::string&) {}

void smoke_test(void*) {}

gentest::Case kCases[] = {
    {
        .name = "regressions/shared_fixture_reentry_smoke",
        .fn = &smoke_test,
        .file = __FILE__,
        .line = 20,
        .is_benchmark = false,
        .is_jitter = false,
        .is_baseline = false,
        .tags = {},
        .requirements = {},
        .skip_reason = {},
        .should_skip = false,
        .fixture = {},
        .fixture_lifetime = gentest::FixtureLifetime::None,
        .suite = "regressions",
    },
};

} // namespace

int main(int argc, char** argv) {
    gentest::detail::SharedFixtureRegistration registration{
        .fixture_name = kFixtureName,
        .suite = std::string_view{},
        .scope = gentest::detail::SharedFixtureScope::Global,
        .create = &create_fixture,
        .setup = &setup_fixture,
        .teardown = &teardown_fixture,
    };
    gentest::detail::register_shared_fixture(registration);
    gentest::detail::register_cases(std::span<const gentest::Case>(kCases));
    return gentest::run_all_tests(argc, argv);
}
