#include "gentest/runner.h"

using namespace gentest::asserts;

namespace regressions::shared_fixture_suite_scope_descendant {

struct ScopeParentFixture : gentest::FixtureSetup {
    int value = 0;
    void setUp() override { value = 42; }
};

void member_case(void *ctx) {
    auto *fixture = static_cast<ScopeParentFixture *>(ctx);
    EXPECT_TRUE(fixture != nullptr, "suite fixture should resolve from declaring parent scope");
    if (!fixture)
        return;
    EXPECT_EQ(fixture->value, 42, "resolved suite fixture should be initialized");
}

gentest::Case kCases[] = {
    {
        .name             = "regressions/shared_fixture_suite_scope_descendant/member_case",
        .fn               = &member_case,
        .file             = __FILE__,
        .line             = 12,
        .is_benchmark     = false,
        .is_jitter        = false,
        .is_baseline      = false,
        .tags             = {},
        .requirements     = {},
        .skip_reason      = {},
        .should_skip      = false,
        .fixture          = "regressions::shared_fixture_suite_scope_descendant::ScopeParentFixture",
        .fixture_lifetime = gentest::FixtureLifetime::MemberSuite,
        .suite            = "regressions/parent/child",
    },
};

struct FixtureRegistrar {
    FixtureRegistrar() {
        gentest::detail::register_shared_fixture<ScopeParentFixture>(gentest::detail::SharedFixtureScope::Suite, "regressions/parent",
                                                                     "regressions::shared_fixture_suite_scope_descendant::ScopeParentFixture");
    }
} kFixtureRegistrar;

} // namespace regressions::shared_fixture_suite_scope_descendant

int main(int argc, char **argv) {
    gentest::detail::register_cases(std::span<const gentest::Case>(regressions::shared_fixture_suite_scope_descendant::kCases));
    return gentest::run_all_tests(argc, argv);
}
