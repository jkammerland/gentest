#include "gentest/detail/fixture_runtime.h"
#include "gentest/detail/registry_runtime.h"
#include "gentest/runner.h"

#include <memory>

using namespace gentest::asserts;

namespace regressions::shared_fixture_global_scope_most_specific {

struct SpecificFixture {
    int marker = 0;

    static std::unique_ptr<SpecificFixture> gentest_allocate(std::string_view suite) {
        auto fx    = std::make_unique<SpecificFixture>();
        fx->marker = (suite == "regressions/parent/child") ? 2 : 1;
        return fx;
    }
};

void member_case(void *ctx) {
    auto *fixture = static_cast<SpecificFixture *>(ctx);
    EXPECT_TRUE(fixture != nullptr, "global fixture should resolve");
    if (!fixture)
        return;
    EXPECT_EQ(fixture->marker, 2, "lookup should use the most specific global registration");
}

gentest::Case kCases[] = {
    {
        .name             = "regressions/shared_fixture_global_scope_most_specific/member_case",
        .fn               = &member_case,
        .file             = __FILE__,
        .line             = 18,
        .is_benchmark     = false,
        .is_jitter        = false,
        .is_baseline      = false,
        .tags             = {},
        .requirements     = {},
        .skip_reason      = {},
        .should_skip      = false,
        .fixture          = "regressions::shared_fixture_global_scope_most_specific::SpecificFixture",
        .fixture_lifetime = gentest::FixtureLifetime::MemberGlobal,
        .suite            = "regressions/parent/child/grandchild",
    },
};

} // namespace regressions::shared_fixture_global_scope_most_specific

int main(int argc, char **argv) {
    constexpr std::string_view kFixtureName = "regressions::shared_fixture_global_scope_most_specific::SpecificFixture";
    gentest::detail::register_shared_fixture<regressions::shared_fixture_global_scope_most_specific::SpecificFixture>(
        gentest::detail::SharedFixtureScope::Global, "regressions/parent", kFixtureName);
    gentest::detail::register_shared_fixture<regressions::shared_fixture_global_scope_most_specific::SpecificFixture>(
        gentest::detail::SharedFixtureScope::Global, "regressions/parent/child", kFixtureName);
    gentest::detail::register_cases(std::span<const gentest::Case>(regressions::shared_fixture_global_scope_most_specific::kCases));
    return gentest::run_all_tests(argc, argv);
}
