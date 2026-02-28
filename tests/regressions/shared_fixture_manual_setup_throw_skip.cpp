#include "gentest/runner.h"

#include <memory>
#include <stdexcept>

namespace {

constexpr std::string_view kFixtureName = "regressions::ThrowingSetupFixture";

std::shared_ptr<void> create_fixture(std::string_view, std::string &) { return std::make_shared<int>(1); }

void setup_throw(void *, std::string &) { throw std::runtime_error("manual-setup-throw"); }

void noop_case(void *) {}

gentest::Case kCases[] = {
    {
        .name             = "regressions/shared_fixture_manual_setup_throw_skip/member_case",
        .fn               = &noop_case,
        .file             = __FILE__,
        .line             = 14,
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
        .create       = &create_fixture,
        .setup        = &setup_throw,
        .teardown     = nullptr,
    });
    gentest::detail::register_cases(std::span<const gentest::Case>(kCases));
    return gentest::run_all_tests(argc, argv);
}
