#include "gentest/runner.h"

#include <memory>
#include <string_view>

using namespace gentest::asserts;

namespace {

constexpr std::string_view kSuiteName      = "regressions/shuffle_fixture_groups";
constexpr std::string_view kSuiteFixtureA  = "regressions::ShuffleSuiteFixtureA";
constexpr std::string_view kSuiteFixtureB  = "regressions::ShuffleSuiteFixtureB";
constexpr std::string_view kGlobalFixtureA = "regressions::ShuffleGlobalFixtureA";
constexpr std::string_view kGlobalFixtureB = "regressions::ShuffleGlobalFixtureB";

enum class Phase {
    FreeCases,
    SuiteGroups,
    GlobalGroups,
};

Phase            g_phase = Phase::FreeCases;
int              g_free_remaining = 2;
int              g_suite_a_remaining = 2;
int              g_suite_b_remaining = 2;
int              g_global_a_remaining = 2;
int              g_global_b_remaining = 2;
std::string_view g_active_suite_group{};
std::string_view g_active_global_group{};

std::shared_ptr<void> create_fixture(std::string_view, std::string &) { return std::make_shared<int>(7); }

int *suite_remaining_ptr(std::string_view fixture) {
    if (fixture == kSuiteFixtureA)
        return &g_suite_a_remaining;
    if (fixture == kSuiteFixtureB)
        return &g_suite_b_remaining;
    return nullptr;
}

int *global_remaining_ptr(std::string_view fixture) {
    if (fixture == kGlobalFixtureA)
        return &g_global_a_remaining;
    if (fixture == kGlobalFixtureB)
        return &g_global_b_remaining;
    return nullptr;
}

bool all_suite_done() { return g_suite_a_remaining == 0 && g_suite_b_remaining == 0; }
bool all_global_done() { return g_global_a_remaining == 0 && g_global_b_remaining == 0; }

void visit_free(void *ctx) {
    EXPECT_TRUE(ctx == nullptr, "free/local cases must not receive fixture context");
    EXPECT_EQ(static_cast<int>(g_phase), static_cast<int>(Phase::FreeCases),
              "free/local cases must execute before fixture groups");
    if (g_phase != Phase::FreeCases)
        return;

    EXPECT_GT(g_free_remaining, 0, "free/local phase executed too many cases");
    if (g_free_remaining <= 0)
        return;

    --g_free_remaining;
    if (g_free_remaining == 0)
        g_phase = Phase::SuiteGroups;
}

void visit_suite_group(std::string_view fixture, void *ctx) {
    EXPECT_TRUE(ctx != nullptr, "suite-shared cases must receive fixture context");

    if (g_phase == Phase::FreeCases) {
        EXPECT_EQ(g_free_remaining, 0, "suite fixture group started before free/local cases finished");
        if (g_free_remaining != 0)
            return;
        g_phase = Phase::SuiteGroups;
    }

    EXPECT_EQ(static_cast<int>(g_phase), static_cast<int>(Phase::SuiteGroups),
              "suite fixture groups must execute before global fixture groups");
    if (g_phase != Phase::SuiteGroups)
        return;

    int *remaining = suite_remaining_ptr(fixture);
    EXPECT_TRUE(remaining != nullptr, "unknown suite fixture group");
    if (!remaining)
        return;

    if (g_active_suite_group.empty()) {
        g_active_suite_group = fixture;
    } else if (g_active_suite_group != fixture) {
        int *active_remaining = suite_remaining_ptr(g_active_suite_group);
        EXPECT_TRUE(active_remaining != nullptr, "active suite fixture group must be known");
        if (!active_remaining)
            return;

        EXPECT_EQ(*active_remaining, 0, "suite fixture groups must not interleave");
        if (*active_remaining != 0)
            return;

        g_active_suite_group = fixture;
    }

    EXPECT_GT(*remaining, 0, "suite fixture group visited more times than registered");
    if (*remaining <= 0)
        return;

    --(*remaining);
    if (all_suite_done())
        g_phase = Phase::GlobalGroups;
}

void visit_global_group(std::string_view fixture, void *ctx) {
    EXPECT_TRUE(ctx != nullptr, "global-shared cases must receive fixture context");

    if (g_phase == Phase::SuiteGroups) {
        EXPECT_TRUE(all_suite_done(), "global fixture groups must start after suite fixture groups complete");
        if (!all_suite_done())
            return;
        g_phase = Phase::GlobalGroups;
    }

    EXPECT_EQ(static_cast<int>(g_phase), static_cast<int>(Phase::GlobalGroups),
              "global fixture groups must execute in global-group phase");
    if (g_phase != Phase::GlobalGroups)
        return;

    int *remaining = global_remaining_ptr(fixture);
    EXPECT_TRUE(remaining != nullptr, "unknown global fixture group");
    if (!remaining)
        return;

    if (g_active_global_group.empty()) {
        g_active_global_group = fixture;
    } else if (g_active_global_group != fixture) {
        int *active_remaining = global_remaining_ptr(g_active_global_group);
        EXPECT_TRUE(active_remaining != nullptr, "active global fixture group must be known");
        if (!active_remaining)
            return;

        EXPECT_EQ(*active_remaining, 0, "global fixture groups must not interleave");
        if (*active_remaining != 0)
            return;

        g_active_global_group = fixture;
    }

    EXPECT_GT(*remaining, 0, "global fixture group visited more times than registered");
    if (*remaining <= 0)
        return;

    --(*remaining);
}

void free_case_one(void *ctx) { visit_free(ctx); }
void free_case_two(void *ctx) { visit_free(ctx); }
void suite_a_case_one(void *ctx) { visit_suite_group(kSuiteFixtureA, ctx); }
void suite_a_case_two(void *ctx) { visit_suite_group(kSuiteFixtureA, ctx); }
void suite_b_case_one(void *ctx) { visit_suite_group(kSuiteFixtureB, ctx); }
void suite_b_case_two(void *ctx) { visit_suite_group(kSuiteFixtureB, ctx); }
void global_a_case_one(void *ctx) { visit_global_group(kGlobalFixtureA, ctx); }
void global_a_case_two(void *ctx) { visit_global_group(kGlobalFixtureA, ctx); }
void global_b_case_one(void *ctx) { visit_global_group(kGlobalFixtureB, ctx); }
void global_b_case_two(void *ctx) { visit_global_group(kGlobalFixtureB, ctx); }

gentest::Case kCases[] = {
    {
        .name             = "regressions/shuffle_fixture_groups/free_case_one",
        .fn               = &free_case_one,
        .file             = __FILE__,
        .line             = 180,
        .is_benchmark     = false,
        .is_jitter        = false,
        .is_baseline      = false,
        .tags             = {},
        .requirements     = {},
        .skip_reason      = {},
        .should_skip      = false,
        .fixture          = {},
        .fixture_lifetime = gentest::FixtureLifetime::None,
        .suite            = kSuiteName,
    },
    {
        .name             = "regressions/shuffle_fixture_groups/suite_a_case_one",
        .fn               = &suite_a_case_one,
        .file             = __FILE__,
        .line             = 196,
        .is_benchmark     = false,
        .is_jitter        = false,
        .is_baseline      = false,
        .tags             = {},
        .requirements     = {},
        .skip_reason      = {},
        .should_skip      = false,
        .fixture          = kSuiteFixtureA,
        .fixture_lifetime = gentest::FixtureLifetime::MemberSuite,
        .suite            = kSuiteName,
    },
    {
        .name             = "regressions/shuffle_fixture_groups/global_a_case_one",
        .fn               = &global_a_case_one,
        .file             = __FILE__,
        .line             = 212,
        .is_benchmark     = false,
        .is_jitter        = false,
        .is_baseline      = false,
        .tags             = {},
        .requirements     = {},
        .skip_reason      = {},
        .should_skip      = false,
        .fixture          = kGlobalFixtureA,
        .fixture_lifetime = gentest::FixtureLifetime::MemberGlobal,
        .suite            = kSuiteName,
    },
    {
        .name             = "regressions/shuffle_fixture_groups/suite_b_case_one",
        .fn               = &suite_b_case_one,
        .file             = __FILE__,
        .line             = 228,
        .is_benchmark     = false,
        .is_jitter        = false,
        .is_baseline      = false,
        .tags             = {},
        .requirements     = {},
        .skip_reason      = {},
        .should_skip      = false,
        .fixture          = kSuiteFixtureB,
        .fixture_lifetime = gentest::FixtureLifetime::MemberSuite,
        .suite            = kSuiteName,
    },
    {
        .name             = "regressions/shuffle_fixture_groups/free_case_two",
        .fn               = &free_case_two,
        .file             = __FILE__,
        .line             = 244,
        .is_benchmark     = false,
        .is_jitter        = false,
        .is_baseline      = false,
        .tags             = {},
        .requirements     = {},
        .skip_reason      = {},
        .should_skip      = false,
        .fixture          = {},
        .fixture_lifetime = gentest::FixtureLifetime::None,
        .suite            = kSuiteName,
    },
    {
        .name             = "regressions/shuffle_fixture_groups/global_b_case_one",
        .fn               = &global_b_case_one,
        .file             = __FILE__,
        .line             = 260,
        .is_benchmark     = false,
        .is_jitter        = false,
        .is_baseline      = false,
        .tags             = {},
        .requirements     = {},
        .skip_reason      = {},
        .should_skip      = false,
        .fixture          = kGlobalFixtureB,
        .fixture_lifetime = gentest::FixtureLifetime::MemberGlobal,
        .suite            = kSuiteName,
    },
    {
        .name             = "regressions/shuffle_fixture_groups/suite_b_case_two",
        .fn               = &suite_b_case_two,
        .file             = __FILE__,
        .line             = 276,
        .is_benchmark     = false,
        .is_jitter        = false,
        .is_baseline      = false,
        .tags             = {},
        .requirements     = {},
        .skip_reason      = {},
        .should_skip      = false,
        .fixture          = kSuiteFixtureB,
        .fixture_lifetime = gentest::FixtureLifetime::MemberSuite,
        .suite            = kSuiteName,
    },
    {
        .name             = "regressions/shuffle_fixture_groups/suite_a_case_two",
        .fn               = &suite_a_case_two,
        .file             = __FILE__,
        .line             = 292,
        .is_benchmark     = false,
        .is_jitter        = false,
        .is_baseline      = false,
        .tags             = {},
        .requirements     = {},
        .skip_reason      = {},
        .should_skip      = false,
        .fixture          = kSuiteFixtureA,
        .fixture_lifetime = gentest::FixtureLifetime::MemberSuite,
        .suite            = kSuiteName,
    },
    {
        .name             = "regressions/shuffle_fixture_groups/global_a_case_two",
        .fn               = &global_a_case_two,
        .file             = __FILE__,
        .line             = 308,
        .is_benchmark     = false,
        .is_jitter        = false,
        .is_baseline      = false,
        .tags             = {},
        .requirements     = {},
        .skip_reason      = {},
        .should_skip      = false,
        .fixture          = kGlobalFixtureA,
        .fixture_lifetime = gentest::FixtureLifetime::MemberGlobal,
        .suite            = kSuiteName,
    },
    {
        .name             = "regressions/shuffle_fixture_groups/global_b_case_two",
        .fn               = &global_b_case_two,
        .file             = __FILE__,
        .line             = 324,
        .is_benchmark     = false,
        .is_jitter        = false,
        .is_baseline      = false,
        .tags             = {},
        .requirements     = {},
        .skip_reason      = {},
        .should_skip      = false,
        .fixture          = kGlobalFixtureB,
        .fixture_lifetime = gentest::FixtureLifetime::MemberGlobal,
        .suite            = kSuiteName,
    },
};

} // namespace

int main(int argc, char **argv) {
    gentest::detail::register_shared_fixture({
        .fixture_name = kSuiteFixtureA,
        .suite        = kSuiteName,
        .scope        = gentest::detail::SharedFixtureScope::Suite,
        .create       = &create_fixture,
        .setup        = nullptr,
        .teardown     = nullptr,
    });
    gentest::detail::register_shared_fixture({
        .fixture_name = kSuiteFixtureB,
        .suite        = kSuiteName,
        .scope        = gentest::detail::SharedFixtureScope::Suite,
        .create       = &create_fixture,
        .setup        = nullptr,
        .teardown     = nullptr,
    });
    gentest::detail::register_shared_fixture({
        .fixture_name = kGlobalFixtureA,
        .suite        = std::string_view{},
        .scope        = gentest::detail::SharedFixtureScope::Global,
        .create       = &create_fixture,
        .setup        = nullptr,
        .teardown     = nullptr,
    });
    gentest::detail::register_shared_fixture({
        .fixture_name = kGlobalFixtureB,
        .suite        = std::string_view{},
        .scope        = gentest::detail::SharedFixtureScope::Global,
        .create       = &create_fixture,
        .setup        = nullptr,
        .teardown     = nullptr,
    });

    gentest::detail::register_cases(std::span<const gentest::Case>(kCases));
    return gentest::run_all_tests(argc, argv);
}
