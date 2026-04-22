#include "gentest/detail/registration_runtime.h"
#include "gentest/detail/registry_runtime.h"
#include "gentest/runner.h"

#include <array>
#include <cstddef>

namespace {

constexpr std::string_view kRegisterCaseName = "regressions/runtime_case_snapshot_isolated/01_register";
constexpr std::string_view kTailCaseName     = "regressions/runtime_case_snapshot_isolated/02_tail";
constexpr std::string_view kLateCaseName     = "regressions/runtime_case_snapshot_isolated/late_registered";
constexpr std::size_t      kLateCaseCount    = 64;
constexpr std::size_t      kInitialCaseCount = 2;

bool        g_registered_late_cases = false;
std::size_t g_late_case_runs        = 0;

void late_registered_case(void *) {
    ++g_late_case_runs;
    gentest::fail("late-registered cases must not run in the active snapshot");
}

void register_late_cases(void *) {
    const auto snapshot = gentest::detail::snapshot_registered_cases();
    gentest::expect_eq(snapshot.size(), kInitialCaseCount, "active run snapshot must contain only the initially scheduled cases");
    gentest::expect_eq(snapshot.front().name, kRegisterCaseName, "run snapshot keeps the first scheduled case");
    gentest::expect_eq(snapshot.back().name, kTailCaseName, "run snapshot keeps the tail case scheduled before registration");

    std::array<gentest::Case, kLateCaseCount> late_cases{};
    for (auto &test_case : late_cases) {
        test_case = {
            .name             = kLateCaseName,
            .fn               = &late_registered_case,
            .file             = __FILE__,
            .line             = __LINE__,
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
        };
    }

    gentest::detail::register_cases(std::span<const gentest::Case>(late_cases));
    g_registered_late_cases = true;

    const auto registry_after_register = gentest::registered_cases();
    gentest::expect_eq(registry_after_register.size(), snapshot.size() + late_cases.size(),
                       "late registration should grow the backing registry after the active run snapshot was taken");
    gentest::expect_eq(g_late_case_runs, std::size_t{0}, "late-registered cases must not execute while the current case is still running");
}

void tail_case(void *) {
    gentest::expect_eq(g_registered_late_cases, true, "tail case must still run after late registration mutates the backing registry");
    gentest::expect_eq(g_late_case_runs, std::size_t{0}, "late-registered cases must not enter the active run before the tail case");
    const auto registry_after_tail = gentest::registered_cases();
    gentest::expect_eq(registry_after_tail.size(), kInitialCaseCount + kLateCaseCount,
                       "tail case should observe the expanded registry without the active run iterating the late additions");
}

gentest::Case kCases[] = {
    {
        .name             = kRegisterCaseName,
        .fn               = &register_late_cases,
        .file             = __FILE__,
        .line             = 0,
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
        .name             = kTailCaseName,
        .fn               = &tail_case,
        .file             = __FILE__,
        .line             = 0,
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
