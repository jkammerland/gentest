#include "cases.hpp"

#include "gentest/analyzer_assertions.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace measurement_tests {
using namespace std::chrono_literals;
using namespace gentest::asserts;

void Sample::setUp() {
    ++setups;
    ++local_setups;
}

void Sample::tearDown() {
    ++local_teardowns;
    independent_failures += setups != 1 || lazy_evaluations != 0;
    if (mode != "no_warmup" && mode != "jitter") {
        independent_failures += calls != 1;
    }
    EXPECT_EQ(setups, 1);
    if (mode != "no_warmup" && mode != "jitter") {
        EXPECT_EQ(calls, 1);
    }
    EXPECT_EQ(lazy_evaluations, 0);

    // Delayed primary data is legal; missing data must not masquerade as a zero sample.
    if (mode == "teardown") {
        EXPECT_FALSE(gentest::currentMeasurement().has_value());
        gentest::setBenchmarkDuration(2s, 1000);
    }
    if (mode == "missing") {
        EXPECT_FALSE(gentest::currentMeasurement().has_value());
        return;
    }

    auto initial = gentest::currentMeasurement();
    if (mode == "call" || mode == "teardown") {
        ASSERT_TRUE(initial.has_value());
        EXPECT_EQ(initial->primary.items_per_call, 1000U);
        EXPECT_EQ(initial->primary.elapsed_seconds, 2.0);
        ASSERT_TRUE(initial->primary.statistics.duration.has_value());
        EXPECT_EQ(initial->primary.statistics.duration->median_ns, 2e9);
        EXPECT_EQ(initial->primary.statistics.duration->p99_ns, 2e9);
        EXPECT_EQ(initial->primary.measured_calls, 1U);
        EXPECT_EQ(initial->primary.calibration_calls, 0U);
        EXPECT_EQ(initial->primary.warmup_epochs, 0U);
        EXPECT_EQ(initial->primary.calibration_seconds, 0.0);
        EXPECT_EQ(initial->primary.warmup_seconds, 0.0);
        EXPECT_TRUE(std::isfinite(initial->primary.measured_wall_seconds));
    }
    if (mode == "no_warmup") {
        ASSERT_TRUE(initial.has_value());
        EXPECT_EQ(initial->primary.warmup_epochs, 0U);
        EXPECT_EQ(initial->primary.warmup_seconds, 0.0);
        EXPECT_EQ(static_cast<std::size_t>(calls), initial->primary.measured_calls + initial->primary.calibration_calls);
    }

    // Primary percentiles must survive every executor's snapshot conversion.
    if (mode == "no_warmup" || mode == "jitter" || mode == "runner") {
        ASSERT_TRUE(initial.has_value());
        ASSERT_TRUE(initial->primary.statistics.duration.has_value());
        const auto &duration = *initial->primary.statistics.duration;
        EXPECT_TRUE(std::isfinite(duration.p99_ns));
        EXPECT_TRUE(duration.p99_ns >= duration.p95_ns);
        EXPECT_TRUE(duration.p99_ns <= duration.max_ns);
        if (mode == "runner") {
            EXPECT_EQ(initial->primary.statistics.count, 1U);
            EXPECT_EQ(duration.p99_ns, duration.median_ns);
        }
    }

    if (mode == "valid") {
        ASSERT_TRUE(initial.has_value());

        // Unequal/overlapping series are independent observations, not portions to subtract.
        const std::array samples{5ns, 1ns, 3ns, 2ns, 4ns};
        gentest::recordTimingSamples("cpu", samples);
        gentest::recordTimingSamples("gpu", std::array<std::uint64_t, 2>{0, std::numeric_limits<std::uint64_t>::max()}, 1ns);
        gentest::recordTimingSamples("unavailable", std::vector<std::chrono::nanoseconds>{});

        auto projected = samples | std::views::transform([](auto duration) { return duration * 2; });
        gentest::recordTimingSamples("overlap", projected);
        std::istringstream single_pass("1 2 3");
        gentest::recordTimingSamples("stream", std::ranges::istream_view<unsigned>(single_pass), 1ns);
        const std::array extremes{0.0, std::numeric_limits<double>::max()};
        gentest::recordTimingSamples("extremes", extremes, 1ns);
        gentest::recordTimingSamples("constant_huge", std::vector<double>(10, std::numeric_limits<double>::max()), 1ns);
        gentest::recordTimingSamples("constant_small", std::vector<double>(10, 1), 1ns);
        gentest::recordTimingSamples("tiny_variance", std::array{0.0, 2e-200}, 1ns);

        // Numeric units are converted before summarizing, including singleton series.
        gentest::recordTimingSamples("singleton_us", std::array{7}, 1us);
        gentest::recordTimingSamples("microseconds", std::array{5, 1, 3, 2, 4}, 1us);

        auto snapshot = gentest::currentMeasurement();
        ASSERT_TRUE(snapshot.has_value());
        ASSERT_EQ(snapshot->timings.size(), 11U);
        EXPECT_TRUE(initial->timings.empty());
        EXPECT_EQ(samples.front(), 5ns);

        // Require every expected distribution before inspecting its numerical values.
        auto      &timings  = snapshot->timings;
        const auto duration = [&timings](std::size_t index) {
            const auto &statistics = timings.at(index).statistics;
            ASSERT_TRUE(statistics.duration.has_value());
            return *statistics.duration;
        };
        const auto cpu = duration(0);
        EXPECT_EQ(cpu.mean_ns, 3.0);
        EXPECT_EQ(cpu.median_ns, 3.0);
        EXPECT_TRUE(std::abs(cpu.stddev_ns - std::sqrt(2.0)) < 1e-12);
        EXPECT_TRUE(std::abs(cpu.p05_ns - 1.2) < 1e-12);
        EXPECT_TRUE(std::abs(cpu.p95_ns - 4.8) < 1e-12);
        EXPECT_TRUE(std::abs(cpu.p99_ns - 4.96) < 1e-12);
        EXPECT_EQ(snapshot->timings[1].statistics.count, 2U);
        EXPECT_EQ(duration(1).min_ns, 0.0);
        EXPECT_EQ(duration(1).max_ns, static_cast<double>(std::numeric_limits<std::uint64_t>::max()));
        EXPECT_EQ(snapshot->timings[2].statistics.count, 0U);
        EXPECT_FALSE(snapshot->timings[2].statistics.duration.has_value());
        EXPECT_EQ(duration(3).mean_ns, 6.0);
        EXPECT_EQ(snapshot->timings[4].statistics.count, 3U);
        EXPECT_TRUE(std::isfinite(duration(5).stddev_ns));
        EXPECT_EQ(duration(5).mean_ns, std::numeric_limits<double>::max() / 2);
        EXPECT_EQ(duration(5).stddev_ns, std::numeric_limits<double>::max() / 2);
        EXPECT_TRUE(std::isfinite(duration(5).p99_ns));
        EXPECT_TRUE(duration(5).p99_ns > duration(5).p95_ns);
        EXPECT_EQ(duration(6).mean_ns, std::numeric_limits<double>::max());
        EXPECT_EQ(duration(6).stddev_ns, 0.0);
        EXPECT_EQ(duration(6).p99_ns, std::numeric_limits<double>::max());
        EXPECT_EQ(duration(7).mean_ns, 1.0);
        EXPECT_EQ(duration(7).stddev_ns, 0.0);
        EXPECT_EQ(duration(8).mean_ns, 1e-200);
        EXPECT_EQ(duration(8).stddev_ns, 1e-200);
        EXPECT_EQ(snapshot->timings[9].statistics.count, 1U);
        EXPECT_EQ(duration(9).p99_ns, 7000.0);
        EXPECT_TRUE(std::abs(duration(10).p99_ns - 4960.0) < 1e-9);

        // The API returns independent owning values, never references to mutable runner storage.
        snapshot->timings[0].name = "changed";
        const auto unchanged      = gentest::currentMeasurement();
        ASSERT_TRUE(unchanged.has_value());
        EXPECT_EQ(unchanged->timings.at(0).name, "cpu");
    }

    if (mode.starts_with("series_")) {
        gentest::recordTimingSamples("retained", std::array{1ns, 2ns});
        if (mode == "series_negative") {
            gentest::recordTimingSamples("bad", std::array{1ns, -1ns});
        } else if (mode == "series_duplicate") {
            auto lazy = std::views::iota(0, 2) | std::views::transform([this](int n) {
                            ++lazy_evaluations;
                            return n;
                        });
            gentest::recordTimingSamples("retained", lazy, 1ns);
            independent_failures += lazy_evaluations != 0;
            EXPECT_EQ(lazy_evaluations, 0);
        } else if (mode == "series_throw") {
            auto lazy = std::views::iota(0, 2) | std::views::transform([](int n) {
                            if (n == 1)
                                throw std::runtime_error("iterator failure");
                            return n;
                        });
            gentest::recordTimingSamples("bad", lazy, 1ns);
        } else if (mode == "series_overflow") {
            gentest::recordTimingSamples("bad", std::array{std::numeric_limits<double>::max()}, 2ns);
        } else if (mode == "series_unit") {
            gentest::recordTimingSamples("bad", std::array{1, 2}, 0ns);
        } else if (mode == "series_nonfinite") {
            gentest::recordTimingSamples("bad", std::array{std::numeric_limits<double>::infinity()}, 1ns);
        } else if (mode == "series_name") {
            gentest::recordTimingSamples("", std::array{1ns});
        } else if (mode == "series_underflow") {
            gentest::recordTimingSamples("bad", std::array{-std::numeric_limits<long double>::denorm_min()},
                                         std::chrono::duration<long double, std::nano>{0.5L});
        } else if (mode == "series_allocation" || mode == "series_summary_allocation") {
            // One sample makes collection exactly one vector allocation; the next is publication scratch/storage.
            allocations_before_failure = mode == "series_allocation" ? 0 : 1;
            gentest::recordTimingSamples("bad", std::array{1ns});
            independent_failures += allocations_before_failure != -1;
            allocations_before_failure = -1;
        }
        const auto retained = gentest::currentMeasurement();
        independent_failures += !retained || retained->timings.size() != 1;
        ASSERT_TRUE(retained.has_value());
        EXPECT_EQ(retained->timings.size(), 1U);
    }

    if (mode == "duplicate")
        gentest::setBenchmarkDuration(3s);
    if (mode == "teardown_failure")
        EXPECT_TRUE(false);

    if (mode == "jitter") {
        gentest::recordTimingSamples("callback", std::array{1ns, 2ns});
        ASSERT_TRUE(initial.has_value());
        EXPECT_EQ(initial->primary.kind, gentest::MeasurementKind::Jitter);
        if (initial->primary.calls_per_sample == 1)
            EXPECT_EQ(initial->primary.sampling, gentest::MeasurementSampling::PerCall);
        EXPECT_TRUE(initial->timings.empty());
        const auto published = gentest::currentMeasurement();
        ASSERT_TRUE(published.has_value());
        EXPECT_EQ(published->timings.size(), 1U);
    }
}

void supplied_call(Sample &sample) {
    ++primary_once_calls[0];
    ++sample.calls;
    sample.mode = "call";
    gentest::setBenchmarkDuration(2s, 1000);
}
void supplied_teardown(Sample &sample) {
    ++primary_once_calls[1];
    ++sample.calls;
    sample.mode = "teardown";
}
void static_items(Sample &sample) {
    ++primary_once_calls[2];
    ++sample.calls;
    gentest::setBenchmarkDuration(2s);
}
void runner_once(Sample &sample) {
    ++primary_once_calls[3];
    ++sample.calls;
    sample.mode = "runner";
}
void no_warmup(Sample &sample) {
    ++sample.calls;
    sample.mode = "no_warmup";
}
void jitter(Sample &sample) {
    ++sample.calls;
    sample.mode = "jitter";
}
void valid_series(Sample &sample) {
    ++sample.calls;
    sample.mode = "valid";
    gentest::setBenchmarkDuration(2s, 1000);
}
void missing(Sample &sample) {
    ++sample.calls;
    sample.mode = "missing";
}
void duplicate(Sample &sample) {
    ++sample.calls;
    sample.mode = "duplicate";
    gentest::setBenchmarkDuration(2s);
}
void zero_duration(Sample &sample) {
    ++sample.calls;
    gentest::setBenchmarkDuration(0s);
}
void zero_items(Sample &sample) {
    ++sample.calls;
    gentest::setBenchmarkDuration(2s, 0);
}
void nan(Sample &sample) {
    ++sample.calls;
    gentest::setBenchmarkDuration(std::chrono::duration<double>{std::numeric_limits<double>::quiet_NaN()});
}
void series_negative(Sample &sample) {
    ++sample.calls;
    sample.mode = "series_negative";
}
void series_duplicate(Sample &sample) {
    ++sample.calls;
    sample.mode = "series_duplicate";
}
void series_throw(Sample &sample) {
    ++sample.calls;
    sample.mode = "series_throw";
}
void series_overflow(Sample &sample) {
    ++sample.calls;
    sample.mode = "series_overflow";
}
void series_unit(Sample &sample) {
    ++sample.calls;
    sample.mode = "series_unit";
}
void teardown_failure(Sample &sample) {
    ++sample.calls;
    sample.mode = "teardown_failure";
    gentest::setBenchmarkDuration(2s);
}
void series_phase(Sample &sample) {
    ++sample.calls;
    auto lazy = std::views::iota(0, 2) | std::views::transform([&sample](int n) {
                    ++sample.lazy_evaluations;
                    return n;
                });
    gentest::recordTimingSamples("bad", lazy, 1ns);
}

void negative_duration(Sample &sample) {
    ++sample.calls;
    gentest::setBenchmarkDuration(-1s);
}
void infinite_duration(Sample &sample) {
    ++sample.calls;
    gentest::setBenchmarkDuration(std::chrono::duration<double>{std::numeric_limits<double>::infinity()});
}
void overflow_duration(Sample &sample) {
    ++sample.calls;
    gentest::setBenchmarkDuration(std::chrono::duration<double>{std::numeric_limits<double>::max()});
}
void runner_supplied(Sample &sample) {
    ++sample.calls;
    gentest::setBenchmarkDuration(1s);
}
void series_nonfinite(Sample &sample) {
    ++sample.calls;
    sample.mode = "series_nonfinite";
}
void series_name(Sample &sample) {
    ++sample.calls;
    sample.mode = "series_name";
}

void series_underflow(Sample &sample) {
    ++sample.calls;
    sample.mode = "series_underflow";
}

void series_allocation(Sample &sample) {
    ++sample.calls;
    sample.mode = "series_allocation";
}
void series_summary_allocation(Sample &sample) {
    ++sample.calls;
    sample.mode = "series_summary_allocation";
}

} // namespace measurement_tests
