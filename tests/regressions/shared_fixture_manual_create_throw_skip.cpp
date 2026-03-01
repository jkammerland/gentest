#include "gentest/runner.h"

#include <stdexcept>

namespace {

constexpr std::string_view kFixtureName = "regressions::ThrowingCreateFixture";

std::shared_ptr<void> create_throw(std::string_view, std::string &) { throw std::runtime_error("manual-create-throw"); }

void noop_case(void *) {}

gentest::Case kCases[] = {
    {
        .name             = "regressions/shared_fixture_manual_create_throw_skip/member_case",
        .fn               = &noop_case,
        .file             = __FILE__,
        .line             = 10,
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
        .create       = &create_throw,
        .setup        = nullptr,
        .teardown     = nullptr,
    });
    gentest::detail::register_cases(std::span<const gentest::Case>(kCases));
    return gentest::run_all_tests(argc, argv);
}
