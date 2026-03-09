#include "runner_measured_format.h"

#include <cmath>
#include <fmt/format.h>

namespace gentest::runner {

double ns_from_s(double s) { return s * 1e9; }

TimeDisplaySpec pick_time_display_spec_from_ns(double abs_ns_max, TimeUnitMode mode) {
    if (mode == TimeUnitMode::Ns)
        return TimeDisplaySpec{
            .unit        = TimeDisplayUnit::Ns,
            .ns_per_unit = 1.0,
            .suffix      = "ns",
        };
    if (abs_ns_max >= 1e9)
        return TimeDisplaySpec{
            .unit        = TimeDisplayUnit::S,
            .ns_per_unit = 1e9,
            .suffix      = "s",
        };
    if (abs_ns_max >= 1e6)
        return TimeDisplaySpec{
            .unit        = TimeDisplayUnit::Ms,
            .ns_per_unit = 1e6,
            .suffix      = "ms",
        };
    if (abs_ns_max >= 1e3)
        return TimeDisplaySpec{
            .unit        = TimeDisplayUnit::Us,
            .ns_per_unit = 1e3,
            .suffix      = "us",
        };
    return TimeDisplaySpec{
        .unit        = TimeDisplayUnit::Ns,
        .ns_per_unit = 1.0,
        .suffix      = "ns",
    };
}

TimeDisplaySpec pick_time_display_spec_from_s(double abs_s_max, TimeUnitMode mode) {
    return pick_time_display_spec_from_ns(ns_from_s(abs_s_max), mode);
}

bool pick_finer_time_display_spec(const TimeDisplaySpec &current, TimeDisplaySpec &out_spec) {
    switch (current.unit) {
    case TimeDisplayUnit::S:
        out_spec = TimeDisplaySpec{
            .unit        = TimeDisplayUnit::Ms,
            .ns_per_unit = 1e6,
            .suffix      = "ms",
        };
        return true;
    case TimeDisplayUnit::Ms:
        out_spec = TimeDisplaySpec{
            .unit        = TimeDisplayUnit::Us,
            .ns_per_unit = 1e3,
            .suffix      = "us",
        };
        return true;
    case TimeDisplayUnit::Us:
        out_spec = TimeDisplaySpec{
            .unit        = TimeDisplayUnit::Ns,
            .ns_per_unit = 1.0,
            .suffix      = "ns",
        };
        return true;
    case TimeDisplayUnit::Ns: return false;
    }
    return false;
}

std::string format_scaled_time_ns(double value_ns, const TimeDisplaySpec &spec) {
    const double scaled = value_ns / spec.ns_per_unit;
    if (spec.unit == TimeDisplayUnit::Ns) {
        const double rounded = std::round(scaled);
        if (std::fabs(rounded - scaled) < 1e-9)
            return fmt::format("{:.0f}", scaled);
        return fmt::format("{:.3f}", scaled);
    }
    return fmt::format("{:.3f}", scaled);
}

std::string format_scaled_time_s(double value_s, const TimeDisplaySpec &spec) { return format_scaled_time_ns(ns_from_s(value_s), spec); }

std::vector<DisplayHistogramBin> make_display_histogram_bins(std::span<const gentest::detail::HistogramBin> bins,
                                                             const TimeDisplaySpec                         &spec) {
    std::vector<DisplayHistogramBin> display_bins;
    display_bins.reserve(bins.size());
    for (const auto &bin : bins) {
        display_bins.push_back(DisplayHistogramBin{
            .lo_text      = format_scaled_time_ns(bin.lo, spec),
            .hi_text      = format_scaled_time_ns(bin.hi, spec),
            .inclusive_hi = bin.inclusive_hi,
            .count        = bin.count,
        });
    }
    return display_bins;
}

bool has_duplicate_display_ranges(std::span<const DisplayHistogramBin> bins) {
    if (bins.size() < 2)
        return false;
    for (std::size_t i = 1; i < bins.size(); ++i) {
        if (bins[i - 1].lo_text == bins[i].lo_text && bins[i - 1].hi_text == bins[i].hi_text)
            return true;
    }
    return false;
}

std::vector<DisplayHistogramBin> merge_duplicate_display_ranges(std::span<const DisplayHistogramBin> bins) {
    std::vector<DisplayHistogramBin> merged;
    if (bins.empty())
        return merged;

    merged.reserve(bins.size());
    merged.push_back(bins.front());
    for (std::size_t i = 1; i < bins.size(); ++i) {
        const auto &bin  = bins[i];
        auto       &last = merged.back();
        if (last.lo_text == bin.lo_text && last.hi_text == bin.hi_text) {
            last.count += bin.count;
            last.inclusive_hi = last.inclusive_hi || bin.inclusive_hi;
            continue;
        }
        merged.push_back(bin);
    }
    return merged;
}

} // namespace gentest::runner
