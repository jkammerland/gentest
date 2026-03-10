#include "gentest/runner.h"

#include <array>
#include <cstddef>

namespace {

void late_registered_case(void *) { gentest::fail("late-registered cases must not run in the active snapshot"); }

void snapshot_survives_late_registration(void *) {
    const auto snapshot = gentest::detail::snapshot_registered_cases();
    gentest::expect_eq(snapshot.size(), std::size_t{1}, "run snapshot starts with the original single case");
    gentest::expect_eq(snapshot.front().name, std::string_view{"regressions/runtime_case_snapshot_isolated/snapshot"},
                       "snapshot keeps the original case identity");

    std::array<gentest::Case, 64> late_cases{};
    for (auto &test_case : late_cases) {
        test_case = {
            .name             = "regressions/runtime_case_snapshot_isolated/late_registered",
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

    const auto count_after_registration = gentest::get_case_count();
    gentest::expect_eq(count_after_registration, snapshot.size() + late_cases.size(),
                       "late registration grows the backing registry after the run snapshot was taken");
    gentest::expect_eq(snapshot.size(), std::size_t{1}, "owned snapshot remains stable after later registration");
    gentest::expect_eq(snapshot.front().name, std::string_view{"regressions/runtime_case_snapshot_isolated/snapshot"},
                       "owned snapshot still refers to the original case after later registration");
}

gentest::Case kCases[] = {
    {
        .name             = "regressions/runtime_case_snapshot_isolated/snapshot",
        .fn               = &snapshot_survives_late_registration,
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
