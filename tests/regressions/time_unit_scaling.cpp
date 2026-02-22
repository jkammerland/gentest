#include "gentest/runner.h"

#include <chrono>
#include <thread>

namespace {

void bench_sleep_ms(void *) { std::this_thread::sleep_for(std::chrono::milliseconds(2)); }

void jitter_sleep_ms(void *) { std::this_thread::sleep_for(std::chrono::milliseconds(2)); }

gentest::Case kCases[] = {
    {
        .name             = "regressions/bench_sleep_ms",
        .fn               = &bench_sleep_ms,
        .file             = __FILE__,
        .line             = __LINE__,
        .is_benchmark     = true,
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
        .name             = "regressions/jitter_sleep_ms",
        .fn               = &jitter_sleep_ms,
        .file             = __FILE__,
        .line             = __LINE__,
        .is_benchmark     = false,
        .is_jitter        = true,
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
