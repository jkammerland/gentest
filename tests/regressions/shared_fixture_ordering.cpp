#include "gentest/runner.h"

#include <memory>

namespace {

constexpr std::string_view kFixtureA = "regressions::FixtureA";
constexpr std::string_view kFixtureB = "regressions::FixtureB";

int g_stage = 0;

std::shared_ptr<void> create_fixture(std::string_view, std::string &) { return std::make_shared<int>(1); }

void setup_a(void *, std::string &error) {
    if (g_stage != 0) {
        error = "A setup must run first";
        return;
    }
    g_stage = 1;
}

void setup_b(void *, std::string &error) {
    if (g_stage != 1) {
        error = "B setup must run after A";
        return;
    }
    g_stage = 2;
}

void teardown_b(void *, std::string &error) {
    if (g_stage != 2) {
        error = "B teardown must run before A teardown";
        return;
    }
    g_stage = 3;
}

void teardown_a(void *, std::string &error) {
    if (g_stage != 3) {
        error = "A teardown must run after B teardown";
        return;
    }
    g_stage = 4;
}

void uses_b(void *) {
    gentest::expect_eq(g_stage, 2, "both setups must complete before test body");
}

gentest::Case kCases[] = {
    {
        .name             = "regressions/shared_fixture_ordering/uses_b",
        .fn               = &uses_b,
        .file             = __FILE__,
        .line             = 41,
        .is_benchmark     = false,
        .is_jitter        = false,
        .is_baseline      = false,
        .tags             = {},
        .requirements     = {},
        .skip_reason      = {},
        .should_skip      = false,
        .fixture          = kFixtureB,
        .fixture_lifetime = gentest::FixtureLifetime::MemberGlobal,
        .suite            = "regressions",
    },
};

} // namespace

int main(int argc, char **argv) {
    // Register in reverse dependency order; runtime must still establish a deterministic safe order.
    gentest::detail::register_shared_fixture({
        .fixture_name = kFixtureB,
        .suite        = std::string_view{},
        .scope        = gentest::detail::SharedFixtureScope::Global,
        .create       = &create_fixture,
        .setup        = &setup_b,
        .teardown     = &teardown_b,
    });
    gentest::detail::register_shared_fixture({
        .fixture_name = kFixtureA,
        .suite        = std::string_view{},
        .scope        = gentest::detail::SharedFixtureScope::Global,
        .create       = &create_fixture,
        .setup        = &setup_a,
        .teardown     = &teardown_a,
    });

    gentest::detail::register_cases(std::span<const gentest::Case>(kCases));
    return gentest::run_all_tests(argc, argv);
}
