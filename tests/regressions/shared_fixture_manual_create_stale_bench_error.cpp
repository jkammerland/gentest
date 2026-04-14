#include "gentest/runner.h"

#include <memory>

namespace {

constexpr std::string_view kFixtureName = "regressions::ClearsStaleBenchErrorFixture";

std::shared_ptr<void> create_fixture(std::string_view, std::string &) { return std::make_shared<int>(1); }

constexpr unsigned kNoopCaseLine = __LINE__ + 1;
void               noop_case(void *) {}

gentest::Case kCases[] = {
    {
        .name             = "regressions/shared_fixture_manual_create_stale_bench_error/member_case",
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
    gentest::detail::record_bench_error("stale-bench-error-marker");
    gentest::detail::register_shared_fixture(gentest::detail::SharedFixtureScope::Global, std::string_view{}, kFixtureName, &create_fixture,
                                             nullptr, nullptr);
    gentest::detail::register_cases(std::span<const gentest::Case>(kCases));
    return gentest::run_all_tests(argc, argv);
}
