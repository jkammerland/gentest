#include "gentest/detail/registration_runtime.h"
#include "gentest/runner.h"

#include <cstdio>
#include <stdexcept>

using namespace gentest::asserts;

namespace {

bool in_bench_call_phase() { return gentest::detail::bench_phase() == gentest::detail::BenchPhase::Call; }

constexpr unsigned kBenchAssertShouldFailLine = __LINE__ + 1;
void               bench_assert_should_fail(void *) {
    if (!in_bench_call_phase())
        return;
    EXPECT_TRUE(false, "intentional benchmark assertion failure");
}

constexpr unsigned kBenchFatalAssertShouldFailLine = __LINE__ + 1;
void               bench_fatal_assert_should_fail(void *) {
    if (!in_bench_call_phase())
        return;
    ASSERT_TRUE(false, "intentional fatal benchmark assertion failure");
}

constexpr unsigned kBenchStdExceptionShouldFailLine = __LINE__ + 1;
void               bench_std_exception_should_fail(void *) {
    if (!in_bench_call_phase())
        return;
    throw std::runtime_error("intentional benchmark std::exception failure");
}

constexpr unsigned kBenchFailShouldFailLine = __LINE__ + 1;
void               bench_fail_should_fail(void *) {
    if (!in_bench_call_phase())
        return;
    gentest::fail("intentional benchmark gentest::failure");
}

constexpr unsigned kBenchSkipShouldFailLine = __LINE__ + 1;
void               bench_skip_should_fail(void *) {
    if (!in_bench_call_phase())
        return;
    gentest::skip("intentional benchmark skip in call phase");
}

constexpr unsigned kBenchSetupSkipShouldNotFailLine = __LINE__ + 1;
void               bench_setup_skip_should_not_fail(void *) {
    if (gentest::detail::bench_phase() != gentest::detail::BenchPhase::Setup)
        return;
    gentest::skip("intentional benchmark setup skip");
}

int                bench_calibration_assert_invocations                  = 0;
constexpr unsigned kBenchCalibrationAssertShouldStopAfterCalibrationLine = __LINE__ + 1;
void               bench_calibration_assert_should_stop_after_calibration(void *) {
    if (!in_bench_call_phase())
        return;
    if (++bench_calibration_assert_invocations == 1)
        EXPECT_TRUE(false, "calibration benchmark assertion failure");
    (void)std::fputs("regression marker: benchmark continued after calibration failure\n", stderr);
}

constexpr unsigned kBenchUnknownExceptionShouldFailLine = __LINE__ + 1;
void               bench_unknown_exception_should_fail(void *) {
    if (!in_bench_call_phase())
        return;
    throw 7;
}

int                bench_warmup_assert_invocations                   = 0;
constexpr unsigned kBenchWarmupAssertShouldStopBeforeMeasurementLine = __LINE__ + 1;
void               bench_warmup_assert_should_stop_before_measurement(void *) {
    if (!in_bench_call_phase())
        return;
    ++bench_warmup_assert_invocations;
    if (bench_warmup_assert_invocations == 2)
        EXPECT_TRUE(false, "warmup benchmark assertion failure");
    if (bench_warmup_assert_invocations > 2)
        (void)std::fputs("regression marker: benchmark continued after warmup failure\n", stderr);
}

constexpr unsigned kJitterAssertShouldFailLine = __LINE__ + 1;
void               jitter_assert_should_fail(void *) {
    if (!in_bench_call_phase())
        return;
    EXPECT_TRUE(false, "intentional jitter assertion failure");
}

constexpr unsigned kJitterFatalAssertShouldFailLine = __LINE__ + 1;
void               jitter_fatal_assert_should_fail(void *) {
    if (!in_bench_call_phase())
        return;
    ASSERT_TRUE(false, "intentional fatal jitter assertion failure");
}

constexpr unsigned kJitterStdExceptionShouldFailLine = __LINE__ + 1;
void               jitter_std_exception_should_fail(void *) {
    if (!in_bench_call_phase())
        return;
    throw std::runtime_error("intentional jitter std::exception failure");
}

constexpr unsigned kJitterFailShouldFailLine = __LINE__ + 1;
void               jitter_fail_should_fail(void *) {
    if (!in_bench_call_phase())
        return;
    gentest::fail("intentional jitter gentest::failure");
}

constexpr unsigned kJitterSkipShouldFailLine = __LINE__ + 1;
void               jitter_skip_should_fail(void *) {
    if (!in_bench_call_phase())
        return;
    gentest::skip("intentional jitter skip in call phase");
}

constexpr unsigned kJitterSetupSkipShouldNotFailLine = __LINE__ + 1;
void               jitter_setup_skip_should_not_fail(void *) {
    if (gentest::detail::bench_phase() != gentest::detail::BenchPhase::Setup)
        return;
    gentest::skip("intentional jitter setup skip");
}

int                jitter_calibration_assert_invocations                  = 0;
constexpr unsigned kJitterCalibrationAssertShouldStopAfterCalibrationLine = __LINE__ + 1;
void               jitter_calibration_assert_should_stop_after_calibration(void *) {
    if (!in_bench_call_phase())
        return;
    if (++jitter_calibration_assert_invocations == 1)
        EXPECT_TRUE(false, "calibration jitter assertion failure");
    (void)std::fputs("regression marker: jitter continued after calibration failure\n", stderr);
}

constexpr unsigned kJitterUnknownExceptionShouldFailLine = __LINE__ + 1;
void               jitter_unknown_exception_should_fail(void *) {
    if (!in_bench_call_phase())
        return;
    throw 7;
}

int                jitter_warmup_assert_invocations                   = 0;
constexpr unsigned kJitterWarmupAssertShouldStopBeforeMeasurementLine = __LINE__ + 1;
void               jitter_warmup_assert_should_stop_before_measurement(void *) {
    if (!in_bench_call_phase())
        return;
    ++jitter_warmup_assert_invocations;
    if (jitter_warmup_assert_invocations == 2)
        EXPECT_TRUE(false, "warmup jitter assertion failure");
    if (jitter_warmup_assert_invocations > 2)
        (void)std::fputs("regression marker: jitter continued after warmup failure\n", stderr);
}

gentest::Case kCases[] = {
    {
        .name             = "regressions/bench_assert_should_fail",
        .fn               = &bench_assert_should_fail,
        .file             = __FILE__,
        .line             = kBenchAssertShouldFailLine,
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
        .name             = "regressions/bench_std_exception_should_fail",
        .fn               = &bench_std_exception_should_fail,
        .file             = __FILE__,
        .line             = kBenchStdExceptionShouldFailLine,
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
        .name             = "regressions/bench_fatal_assert_should_fail",
        .fn               = &bench_fatal_assert_should_fail,
        .file             = __FILE__,
        .line             = kBenchFatalAssertShouldFailLine,
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
        .name             = "regressions/bench_fail_should_fail",
        .fn               = &bench_fail_should_fail,
        .file             = __FILE__,
        .line             = kBenchFailShouldFailLine,
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
        .name             = "regressions/bench_skip_should_fail",
        .fn               = &bench_skip_should_fail,
        .file             = __FILE__,
        .line             = kBenchSkipShouldFailLine,
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
        .name             = "regressions/bench_setup_skip_should_not_fail",
        .fn               = &bench_setup_skip_should_not_fail,
        .file             = __FILE__,
        .line             = kBenchSetupSkipShouldNotFailLine,
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
        .name             = "regressions/bench_calibration_assert_should_stop_after_calibration",
        .fn               = &bench_calibration_assert_should_stop_after_calibration,
        .file             = __FILE__,
        .line             = kBenchCalibrationAssertShouldStopAfterCalibrationLine,
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
        .name             = "regressions/bench_unknown_exception_should_fail",
        .fn               = &bench_unknown_exception_should_fail,
        .file             = __FILE__,
        .line             = kBenchUnknownExceptionShouldFailLine,
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
        .name             = "regressions/bench_warmup_assert_should_stop_before_measurement",
        .fn               = &bench_warmup_assert_should_stop_before_measurement,
        .file             = __FILE__,
        .line             = kBenchWarmupAssertShouldStopBeforeMeasurementLine,
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
        .name             = "regressions/jitter_assert_should_fail",
        .fn               = &jitter_assert_should_fail,
        .file             = __FILE__,
        .line             = kJitterAssertShouldFailLine,
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
    {
        .name             = "regressions/jitter_std_exception_should_fail",
        .fn               = &jitter_std_exception_should_fail,
        .file             = __FILE__,
        .line             = kJitterStdExceptionShouldFailLine,
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
    {
        .name             = "regressions/jitter_fatal_assert_should_fail",
        .fn               = &jitter_fatal_assert_should_fail,
        .file             = __FILE__,
        .line             = kJitterFatalAssertShouldFailLine,
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
    {
        .name             = "regressions/jitter_fail_should_fail",
        .fn               = &jitter_fail_should_fail,
        .file             = __FILE__,
        .line             = kJitterFailShouldFailLine,
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
    {
        .name             = "regressions/jitter_skip_should_fail",
        .fn               = &jitter_skip_should_fail,
        .file             = __FILE__,
        .line             = kJitterSkipShouldFailLine,
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
    {
        .name             = "regressions/jitter_setup_skip_should_not_fail",
        .fn               = &jitter_setup_skip_should_not_fail,
        .file             = __FILE__,
        .line             = kJitterSetupSkipShouldNotFailLine,
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
    {
        .name             = "regressions/jitter_calibration_assert_should_stop_after_calibration",
        .fn               = &jitter_calibration_assert_should_stop_after_calibration,
        .file             = __FILE__,
        .line             = kJitterCalibrationAssertShouldStopAfterCalibrationLine,
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
    {
        .name             = "regressions/jitter_unknown_exception_should_fail",
        .fn               = &jitter_unknown_exception_should_fail,
        .file             = __FILE__,
        .line             = kJitterUnknownExceptionShouldFailLine,
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
    {
        .name             = "regressions/jitter_warmup_assert_should_stop_before_measurement",
        .fn               = &jitter_warmup_assert_should_stop_before_measurement,
        .file             = __FILE__,
        .line             = kJitterWarmupAssertShouldStopBeforeMeasurementLine,
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
