#include "gentest/detail/generated_runtime.h"
#include "gentest/runner.h"

namespace {

constexpr std::string_view kFixtureName = "regressions::MissingFactoryFixture";

void noop_case(void *) {}

gentest::Case kCases[] = {
    {
        .name             = "regressions/shared_fixture_missing_factory_skip/member_case",
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
    gentest::detail::register_shared_fixture(gentest::detail::SharedFixtureScope::Global, std::string_view{}, kFixtureName, nullptr,
                                             nullptr, nullptr);
    gentest::detail::register_cases(std::span<const gentest::Case>(kCases));
    return gentest::run_all_tests(argc, argv);
}
