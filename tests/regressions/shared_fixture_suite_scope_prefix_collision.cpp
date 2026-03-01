#include "gentest/runner.h"

using namespace gentest::asserts;

namespace regressions::shared_fixture_suite_scope_prefix_collision {

struct PrefixCollisionFixture {
    int value = 7;
};

void member_case(void *ctx) {
    auto *fixture = static_cast<PrefixCollisionFixture *>(ctx);
    EXPECT_TRUE(fixture != nullptr, "fixture resolution unexpectedly succeeded");
    if (!fixture)
        return;
    EXPECT_EQ(fixture->value, 7, "resolved fixture should be initialized");
}

gentest::Case kCases[] = {
    {
        .name             = "regressions/shared_fixture_suite_scope_prefix_collision/member_case",
        .fn               = &member_case,
        .file             = __FILE__,
        .line             = 10,
        .is_benchmark     = false,
        .is_jitter        = false,
        .is_baseline      = false,
        .tags             = {},
        .requirements     = {},
        .skip_reason      = {},
        .should_skip      = false,
        .fixture          = "regressions::shared_fixture_suite_scope_prefix_collision::PrefixCollisionFixture",
        .fixture_lifetime = gentest::FixtureLifetime::MemberSuite,
        .suite            = "regressions/parental/child",
    },
};

struct FixtureRegistrar {
    FixtureRegistrar() {
        gentest::detail::register_shared_fixture<PrefixCollisionFixture>(
            gentest::detail::SharedFixtureScope::Suite, "regressions/parent",
            "regressions::shared_fixture_suite_scope_prefix_collision::PrefixCollisionFixture");
    }
} kFixtureRegistrar;

} // namespace regressions::shared_fixture_suite_scope_prefix_collision

int main(int argc, char **argv) {
    gentest::detail::register_cases(std::span<const gentest::Case>(regressions::shared_fixture_suite_scope_prefix_collision::kCases));
    return gentest::run_all_tests(argc, argv);
}
