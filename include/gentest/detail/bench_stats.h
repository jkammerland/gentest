#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace gentest::detail {

struct SampleStats {
    std::size_t count = 0;
    double      min = 0.0;
    double      max = 0.0;
    double      median = 0.0;
    double      mean = 0.0;
    double      stddev = 0.0;
    double      p05 = 0.0;
    double      p95 = 0.0;
};

struct HistogramBin {
    double      lo = 0.0;
    double      hi = 0.0;
    std::size_t count = 0;
    double      percent = 0.0;
    double      cumulative_percent = 0.0;
    bool        inclusive_hi = false;
};

struct Histogram {
    std::vector<HistogramBin> bins;
};

SampleStats compute_sample_stats(std::span<const double> samples);
Histogram compute_histogram(std::span<const double> samples, int bins);

} // namespace gentest::detail
