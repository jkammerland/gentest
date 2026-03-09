#pragma once

#include "gentest/detail/bench_stats.h"
#include "runner_cli.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gentest::runner {

double ns_from_s(double s);

enum class TimeDisplayUnit {
    Ns,
    Us,
    Ms,
    S,
};

struct TimeDisplaySpec {
    TimeDisplayUnit  unit        = TimeDisplayUnit::Ns;
    double           ns_per_unit = 1.0;
    std::string_view suffix      = "ns";
};

TimeDisplaySpec pick_time_display_spec_from_ns(double abs_ns_max, TimeUnitMode mode);
TimeDisplaySpec pick_time_display_spec_from_s(double abs_s_max, TimeUnitMode mode);
bool            pick_finer_time_display_spec(const TimeDisplaySpec &current, TimeDisplaySpec &out_spec);
std::string     format_scaled_time_ns(double value_ns, const TimeDisplaySpec &spec);
std::string     format_scaled_time_s(double value_s, const TimeDisplaySpec &spec);

struct DisplayHistogramBin {
    std::string lo_text;
    std::string hi_text;
    bool        inclusive_hi = false;
    std::size_t count        = 0;
};

std::vector<DisplayHistogramBin> make_display_histogram_bins(std::span<const gentest::detail::HistogramBin> bins,
                                                             const TimeDisplaySpec                         &spec);
bool                             has_duplicate_display_ranges(std::span<const DisplayHistogramBin> bins);
std::vector<DisplayHistogramBin> merge_duplicate_display_ranges(std::span<const DisplayHistogramBin> bins);

} // namespace gentest::runner
