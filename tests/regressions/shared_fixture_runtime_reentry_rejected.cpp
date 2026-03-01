#include "gentest/runner.h"

#include <atomic>

namespace {

std::atomic<int> g_inner_runs{0};

void inner_case(void *) { g_inner_runs.fetch_add(1, std::memory_order_relaxed); }

void outer_case(void *) {
    const char *inner_args[] = {
        "gentest",
        "--run=regressions/shared_fixture_runtime_reentry_rejected/inner",
        "--kind=test",
    };
    const int rc = gentest::run_all_tests(std::span<const char *>(inner_args, 3));
    gentest::expect(rc == 1, "nested run should fail when shared fixture runtime gate is already active");
    gentest::expect(g_inner_runs.load(std::memory_order_relaxed) == 0, "nested run should not execute selected cases");
}

gentest::Case kCases[] = {
    {
        .name             = "regressions/shared_fixture_runtime_reentry_rejected/outer",
        .fn               = &outer_case,
        .file             = __FILE__,
        .line             = 13,
        .is_benchmark     = false,
        .is_jitter        = false,
        .is_baseline      = false,
        .tags             = {},
        .requirements     = {},
        .skip_reason      = {},
        .should_skip      = false,
        .fixture          = {},
        .fixture_lifetime = gentest::FixtureLifetime::None,
        .suite            = "regressions",
    },
    {
        .name             = "regressions/shared_fixture_runtime_reentry_rejected/inner",
        .fn               = &inner_case,
        .file             = __FILE__,
        .line             = 8,
        .is_benchmark     = false,
        .is_jitter        = false,
        .is_baseline      = false,
        .tags             = {},
        .requirements     = {},
        .skip_reason      = {},
        .should_skip      = false,
        .fixture          = {},
        .fixture_lifetime = gentest::FixtureLifetime::None,
        .suite            = "regressions",
    },
};

} // namespace

int main(int argc, char **argv) {
    gentest::detail::register_cases(std::span<const gentest::Case>(kCases));
    return gentest::run_all_tests(argc, argv);
}
