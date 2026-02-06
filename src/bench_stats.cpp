#include "gentest/detail/bench_stats.h"

#include <algorithm>
#include <cmath>

namespace gentest::detail {
namespace {
double percentile_sorted(const std::vector<double>& v, double p) {
    if (v.empty())
        return 0.0;
    if (v.size() == 1)
        return v.front();
    if (p <= 0.0)
        return v.front();
    if (p >= 1.0)
        return v.back();
    const double idx = p * static_cast<double>(v.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(idx);
    const std::size_t hi = (lo + 1 < v.size()) ? (lo + 1) : lo;
    const double frac = idx - static_cast<double>(lo);
    return v[lo] + (v[hi] - v[lo]) * frac;
}

double mean_of(const std::vector<double>& v) {
    if (v.empty())
        return 0.0;
    double s = 0.0;
    for (double x : v)
        s += x;
    return s / static_cast<double>(v.size());
}

double stddev_of(const std::vector<double>& v, double mean) {
    if (v.size() < 2)
        return 0.0;
    double sum = 0.0;
    for (double x : v) {
        const double d = x - mean;
        sum += d * d;
    }
    return std::sqrt(sum / static_cast<double>(v.size()));
}
} // namespace

SampleStats compute_sample_stats(std::span<const double> samples) {
    SampleStats stats{};
    stats.count = samples.size();
    if (samples.empty())
        return stats;

    std::vector<double> sorted(samples.begin(), samples.end());
    std::sort(sorted.begin(), sorted.end());
    stats.min = sorted.front();
    stats.max = sorted.back();
    stats.median = percentile_sorted(sorted, 0.5);
    stats.p05 = percentile_sorted(sorted, 0.05);
    stats.p95 = percentile_sorted(sorted, 0.95);
    stats.mean = mean_of(sorted);
    stats.stddev = stddev_of(sorted, stats.mean);
    return stats;
}

Histogram compute_histogram(std::span<const double> samples, int bins) {
    Histogram hist{};
    if (samples.empty())
        return hist;

    const int safe_bins = (bins > 0) ? bins : 1;
    const auto min_it = std::min_element(samples.begin(), samples.end());
    const auto max_it = std::max_element(samples.begin(), samples.end());
    const double min_v = *min_it;
    const double max_v = *max_it;
    const int hist_bins = (min_v == max_v) ? 1 : safe_bins;
    const double width = (hist_bins == 1) ? 0.0 : (max_v - min_v) / static_cast<double>(hist_bins);

    std::vector<std::size_t> counts(static_cast<std::size_t>(hist_bins), 0);
    for (double v : samples) {
        int idx = 0;
        if (hist_bins > 1) {
            const double offset = (v - min_v) / width;
            idx = static_cast<int>(offset);
            if (idx < 0) idx = 0;
            if (idx >= hist_bins) idx = hist_bins - 1;
        }
        counts[static_cast<std::size_t>(idx)]++;
    }

    const double total = static_cast<double>(samples.size());
    std::size_t cumulative = 0;
    hist.bins.reserve(static_cast<std::size_t>(hist_bins));
    for (int i = 0; i < hist_bins; ++i) {
        const double lo = (hist_bins == 1) ? min_v : (min_v + width * static_cast<double>(i));
        const double hi = (hist_bins == 1) ? max_v
                                           : ((i == hist_bins - 1) ? max_v
                                                                   : (min_v + width * static_cast<double>(i + 1)));
        const std::size_t count = counts[static_cast<std::size_t>(i)];
        cumulative += count;
        const double pct = (total > 0.0) ? (static_cast<double>(count) / total * 100.0) : 0.0;
        const double cum_pct = (total > 0.0) ? (static_cast<double>(cumulative) / total * 100.0) : 0.0;
        hist.bins.push_back(HistogramBin{
            lo,
            hi,
            count,
            pct,
            cum_pct,
            (i == hist_bins - 1),
        });
    }
    return hist;
}

} // namespace gentest::detail
