#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Must remain a macro: generated registrations test this capability with #if.
// NOLINTNEXTLINE(modernize-macro-to-enum)
#define GENTEST_BENCHMARK_API_VERSION 1

namespace gentest {

enum class BenchmarkExecution { Calibrated, Once };
enum class BenchmarkTimeSource { Runner, Supplied };

struct BenchmarkOptions {
    BenchmarkExecution         execution   = BenchmarkExecution::Calibrated;
    BenchmarkTimeSource        time_source = BenchmarkTimeSource::Runner;
    std::optional<std::size_t> warmup_epochs;
};

enum class MeasurementKind { Benchmark, Jitter };
enum class MeasurementSampling { EpochAverage, PerCall, BatchAverage, SingleCall };

struct DurationSummary {
    double min_ns    = 0;
    double max_ns    = 0;
    double mean_ns   = 0;
    double median_ns = 0;
    double stddev_ns = 0; // Population standard deviation.
    double p05_ns    = 0;
    double p95_ns    = 0;
    double p99_ns    = 0;
};

struct TimingStatistics {
    std::size_t                    count = 0;
    std::optional<DurationSummary> duration;
};

struct NamedTimingStatistics {
    std::string      name;
    TimingStatistics statistics;
};

struct PrimaryMeasurement {
    MeasurementKind     kind        = MeasurementKind::Benchmark;
    BenchmarkExecution  execution   = BenchmarkExecution::Calibrated;
    BenchmarkTimeSource time_source = BenchmarkTimeSource::Runner;
    MeasurementSampling sampling    = MeasurementSampling::EpochAverage;
    TimingStatistics    statistics;
    std::size_t         measured_calls        = 0;
    std::size_t         calls_per_sample      = 0;
    std::uint64_t       items_per_call        = 1;
    double              elapsed_seconds       = 0;
    double              measured_wall_seconds = 0;
    double              warmup_seconds        = 0;
    double              calibration_seconds   = 0;
    std::size_t         calibration_calls     = 0;
    std::size_t         warmup_epochs         = 0;
};

// An owning view of publications so far, not the final case outcome.
struct MeasurementSnapshot {
    PrimaryMeasurement                 primary;
    std::vector<NamedTimingStatistics> timings;
};

} // namespace gentest
