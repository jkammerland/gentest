#include "cases.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <vector>

int main(int argc, char **argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--manual-invalid") {
        using enum gentest::BenchmarkExecution;
        using enum gentest::BenchmarkTimeSource;
        const std::array policies{
            gentest::BenchmarkOptions{.execution = Once, .warmup_epochs = 1},
            gentest::BenchmarkOptions{.time_source = Supplied},
            // Deliberately invalid descriptors must be rejected before fixture setup.
            // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
            gentest::BenchmarkOptions{.execution = static_cast<gentest::BenchmarkExecution>(99)},
            // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
            gentest::BenchmarkOptions{.time_source = static_cast<gentest::BenchmarkTimeSource>(99)},
        };
        for (const auto &policy : policies) {
            const gentest::Case test{
                .name             = "measurement_tests/manual_invalid",
                .fn               = +[](void *) { ++measurement_tests::independent_failures; },
                .file             = __FILE__,
                .line             = __LINE__,
                .is_benchmark     = true,
                .fixture_lifetime = gentest::FixtureLifetime::None,
                .suite            = "measurement_tests",
                .benchmark        = policy,
            };
            const char *options[]{"--kind=bench"};
            const int   result = gentest::run_cases(std::span(&test, 1), options);
            if (result != 1 || measurement_tests::shared_setups != 0 || measurement_tests::independent_failures != 0) {
                (void)std::fputs("invalid manual benchmark reached fixture setup or call\n", stderr);
                return 86;
            }
        }
        return 0;
    }

    std::vector<const char *> args(argv, argv + argc);
    const int                 result = gentest::run_all_tests(args);

    // An intentionally failed occurrence must not mask a broken teardown/preflight probe.
    // Inspect outside the runner because its failure report may contain only the first error.
    // Persistent counts also catch repeated whole lifecycles with fresh local fixtures.
    if (measurement_tests::independent_failures != 0 || measurement_tests::local_setups != measurement_tests::local_teardowns ||
        std::ranges::any_of(measurement_tests::primary_once_calls, [](int count) { return count > 1; }) ||
        (measurement_tests::local_setups > 0 && measurement_tests::shared_setups != 1)) {
        (void)std::fputs("independent measurement contract probe failed\n", stderr);
        return 86;
    }
    return result;
}
