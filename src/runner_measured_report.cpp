#include "runner_measured_report.h"

#include "gentest/detail/bench_stats.h"
#include "runner_measured_format.h"

#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <iostream>
#include <map>
#include <tabulate/table.hpp>

namespace gentest::runner {
namespace {

using tabulate::FontAlign;
using tabulate::Table;
using Row_t = Table::Row_t;

} // namespace

void print_bench_report(std::span<const BenchReportRow> rows, const CliOptions &opt) {
    std::map<std::string, double> baseline_ns;
    for (const auto &row : rows) {
        if (!row.c || !row.c->is_baseline)
            continue;
        const std::string suite(row.c->suite);
        if (baseline_ns.find(suite) == baseline_ns.end()) {
            baseline_ns.emplace(suite, row.result.median_ns);
        }
    }

    const auto bench_calls_per_sec = [](const BenchResult &result) -> double {
        if (result.total_time_s <= 0.0 || result.total_iters == 0)
            return 0.0;
        return static_cast<double>(result.total_iters) / result.total_time_s;
    };

    const auto bench_max_abs_ns = [&](const auto &projector) {
        double max_abs = 0.0;
        for (const auto &row : rows) {
            if (!row.c)
                continue;
            max_abs = std::max(max_abs, std::fabs(projector(row.result)));
        }
        return max_abs;
    };
    const auto bench_max_abs_s = [&](const auto &projector) {
        double max_abs = 0.0;
        for (const auto &row : rows) {
            if (!row.c)
                continue;
            max_abs = std::max(max_abs, std::fabs(projector(row.result)));
        }
        return max_abs;
    };

    const TimeDisplaySpec median_spec =
        pick_time_display_spec_from_ns(bench_max_abs_ns([](const BenchResult &result) { return result.median_ns; }), opt.time_unit_mode);
    const TimeDisplaySpec mean_spec =
        pick_time_display_spec_from_ns(bench_max_abs_ns([](const BenchResult &result) { return result.mean_ns; }), opt.time_unit_mode);
    const TimeDisplaySpec p05_spec =
        pick_time_display_spec_from_ns(bench_max_abs_ns([](const BenchResult &result) { return result.p05_ns; }), opt.time_unit_mode);
    const TimeDisplaySpec p95_spec =
        pick_time_display_spec_from_ns(bench_max_abs_ns([](const BenchResult &result) { return result.p95_ns; }), opt.time_unit_mode);
    const TimeDisplaySpec worst_spec =
        pick_time_display_spec_from_ns(bench_max_abs_ns([](const BenchResult &result) { return result.worst_ns; }), opt.time_unit_mode);
    const TimeDisplaySpec total_spec =
        pick_time_display_spec_from_s(bench_max_abs_s([](const BenchResult &result) { return result.wall_time_s; }), opt.time_unit_mode);

    const TimeDisplaySpec measured_debug_spec =
        pick_time_display_spec_from_s(bench_max_abs_s([](const BenchResult &result) { return result.total_time_s; }), opt.time_unit_mode);
    const TimeDisplaySpec wall_debug_spec =
        pick_time_display_spec_from_s(bench_max_abs_s([](const BenchResult &result) { return result.wall_time_s; }), opt.time_unit_mode);
    const TimeDisplaySpec warmup_debug_spec =
        pick_time_display_spec_from_s(bench_max_abs_s([](const BenchResult &result) { return result.warmup_time_s; }), opt.time_unit_mode);
    const TimeDisplaySpec calib_debug_spec =
        pick_time_display_spec_from_s(bench_max_abs_s([](const BenchResult &result) { return result.calibration_time_s; }),
                                      opt.time_unit_mode);
    const TimeDisplaySpec min_epoch_debug_spec =
        pick_time_display_spec_from_s(std::fabs(opt.bench_cfg.min_epoch_time_s), opt.time_unit_mode);
    const TimeDisplaySpec min_total_debug_spec =
        pick_time_display_spec_from_s(std::fabs(opt.bench_cfg.min_total_time_s), opt.time_unit_mode);
    const TimeDisplaySpec max_total_debug_spec =
        pick_time_display_spec_from_s(std::fabs(opt.bench_cfg.max_total_time_s), opt.time_unit_mode);

    Table summary;
    summary.add_row(Row_t{"Benchmark", "Samples", "Iters/epoch", fmt::format("Median ({}/op)", median_spec.suffix),
                          fmt::format("Mean ({}/op)", mean_spec.suffix), fmt::format("P05 ({}/op)", p05_spec.suffix),
                          fmt::format("P95 ({}/op)", p95_spec.suffix), fmt::format("Worst ({}/op)", worst_spec.suffix),
                          fmt::format("Total ({})", total_spec.suffix), "Baseline Δ%"});
    summary[0].format().font_align(FontAlign::center);
    summary.column(1).format().font_align(FontAlign::right);
    summary.column(2).format().font_align(FontAlign::right);
    summary.column(3).format().font_align(FontAlign::right);
    summary.column(4).format().font_align(FontAlign::right);
    summary.column(5).format().font_align(FontAlign::right);
    summary.column(6).format().font_align(FontAlign::right);
    summary.column(7).format().font_align(FontAlign::right);
    summary.column(8).format().font_align(FontAlign::right);
    summary.column(9).format().font_align(FontAlign::right);

    for (const auto &row : rows) {
        if (!row.c)
            continue;
        const std::string suite(row.c->suite);
        const auto        base_it = baseline_ns.find(suite);
        const double      base_ns = (base_it == baseline_ns.end()) ? 0.0 : base_it->second;
        const std::string baseline_cell =
            (base_ns > 0.0) ? fmt::format("{:+.2f}%", (row.result.median_ns - base_ns) / base_ns * 100.0) : std::string("-");
        summary.add_row(Row_t{
            std::string(row.c->name),
            fmt::format("{}", row.result.epochs),
            fmt::format("{}", row.result.iters_per_epoch),
            format_scaled_time_ns(row.result.median_ns, median_spec),
            format_scaled_time_ns(row.result.mean_ns, mean_spec),
            format_scaled_time_ns(row.result.p05_ns, p05_spec),
            format_scaled_time_ns(row.result.p95_ns, p95_spec),
            format_scaled_time_ns(row.result.worst_ns, worst_spec),
            format_scaled_time_s(row.result.wall_time_s, total_spec),
            baseline_cell,
        });
    }

    Table debug;
    debug.add_row(Row_t{"Benchmark", "Epochs", "Iters/epoch", "Total iters", fmt::format("Measured ({})", measured_debug_spec.suffix),
                        fmt::format("Wall ({})", wall_debug_spec.suffix), fmt::format("Warmup ({})", warmup_debug_spec.suffix),
                        "Calib iters", fmt::format("Calib ({})", calib_debug_spec.suffix),
                        fmt::format("Min epoch ({})", min_epoch_debug_spec.suffix),
                        fmt::format("Min total ({})", min_total_debug_spec.suffix),
                        fmt::format("Max total ({})", max_total_debug_spec.suffix), "Calls/sec"});
    debug[0].format().font_align(FontAlign::center);
    for (std::size_t col = 1; col < 13; ++col) {
        debug.column(col).format().font_align(FontAlign::right);
    }

    for (const auto &row : rows) {
        if (!row.c)
            continue;
        debug.add_row(Row_t{
            std::string(row.c->name),
            fmt::format("{}", row.result.epochs),
            fmt::format("{}", row.result.iters_per_epoch),
            fmt::format("{}", row.result.total_iters),
            format_scaled_time_s(row.result.total_time_s, measured_debug_spec),
            format_scaled_time_s(row.result.wall_time_s, wall_debug_spec),
            format_scaled_time_s(row.result.warmup_time_s, warmup_debug_spec),
            fmt::format("{}", row.result.calibration_iters),
            format_scaled_time_s(row.result.calibration_time_s, calib_debug_spec),
            format_scaled_time_s(opt.bench_cfg.min_epoch_time_s, min_epoch_debug_spec),
            format_scaled_time_s(opt.bench_cfg.min_total_time_s, min_total_debug_spec),
            format_scaled_time_s(opt.bench_cfg.max_total_time_s, max_total_debug_spec),
            fmt::format("{:.3f}", bench_calls_per_sec(row.result)),
        });
    }

    std::cout << "Benchmarks\n" << summary << "\n\n";
    std::cout << "Bench debug\n" << debug << "\n";
}

void print_jitter_report(std::span<const JitterReportRow> rows, const CliOptions &opt) {
    const int bins = opt.jitter_bins;
    std::map<std::string, double> baseline_median_ns;
    std::map<std::string, double> baseline_stddev_ns;
    for (const auto &row : rows) {
        if (!row.c || !row.c->is_baseline)
            continue;
        const std::string suite(row.c->suite);
        if (baseline_median_ns.find(suite) == baseline_median_ns.end()) {
            baseline_median_ns.emplace(suite, row.result.median_ns);
            baseline_stddev_ns.emplace(suite, row.result.stddev_ns);
        }
    }

    const auto jitter_max_abs_ns = [&](const auto &projector) {
        double max_abs = 0.0;
        for (const auto &row : rows) {
            if (!row.c)
                continue;
            max_abs = std::max(max_abs, std::fabs(projector(row.result)));
        }
        return max_abs;
    };
    const auto jitter_max_abs_s = [&](const auto &projector) {
        double max_abs = 0.0;
        for (const auto &row : rows) {
            if (!row.c)
                continue;
            max_abs = std::max(max_abs, std::fabs(projector(row.result)));
        }
        return max_abs;
    };

    const TimeDisplaySpec median_spec =
        pick_time_display_spec_from_ns(jitter_max_abs_ns([](const JitterResult &result) { return result.median_ns; }), opt.time_unit_mode);
    const TimeDisplaySpec mean_spec =
        pick_time_display_spec_from_ns(jitter_max_abs_ns([](const JitterResult &result) { return result.mean_ns; }), opt.time_unit_mode);
    const TimeDisplaySpec stddev_spec =
        pick_time_display_spec_from_ns(jitter_max_abs_ns([](const JitterResult &result) { return result.stddev_ns; }), opt.time_unit_mode);
    const TimeDisplaySpec p05_spec =
        pick_time_display_spec_from_ns(jitter_max_abs_ns([](const JitterResult &result) { return result.p05_ns; }), opt.time_unit_mode);
    const TimeDisplaySpec p95_spec =
        pick_time_display_spec_from_ns(jitter_max_abs_ns([](const JitterResult &result) { return result.p95_ns; }), opt.time_unit_mode);
    const TimeDisplaySpec min_spec =
        pick_time_display_spec_from_ns(jitter_max_abs_ns([](const JitterResult &result) { return result.min_ns; }), opt.time_unit_mode);
    const TimeDisplaySpec max_spec =
        pick_time_display_spec_from_ns(jitter_max_abs_ns([](const JitterResult &result) { return result.max_ns; }), opt.time_unit_mode);
    const TimeDisplaySpec total_spec =
        pick_time_display_spec_from_s(jitter_max_abs_s([](const JitterResult &result) { return result.wall_time_s; }), opt.time_unit_mode);

    double overhead_abs_max_ns = 0.0;
    for (const auto &row : rows) {
        if (!row.c)
            continue;
        overhead_abs_max_ns = std::max(overhead_abs_max_ns, std::fabs(row.result.overhead_mean_ns));
        overhead_abs_max_ns = std::max(overhead_abs_max_ns, std::fabs(row.result.overhead_sd_ns));
    }
    const TimeDisplaySpec overhead_spec = pick_time_display_spec_from_ns(overhead_abs_max_ns, opt.time_unit_mode);
    const TimeDisplaySpec measured_debug_spec =
        pick_time_display_spec_from_s(jitter_max_abs_s([](const JitterResult &result) { return result.total_time_s; }), opt.time_unit_mode);
    const TimeDisplaySpec warmup_debug_spec =
        pick_time_display_spec_from_s(jitter_max_abs_s([](const JitterResult &result) { return result.warmup_time_s; }), opt.time_unit_mode);
    const TimeDisplaySpec wall_debug_spec =
        pick_time_display_spec_from_s(jitter_max_abs_s([](const JitterResult &result) { return result.wall_time_s; }), opt.time_unit_mode);
    const TimeDisplaySpec min_total_debug_spec =
        pick_time_display_spec_from_s(std::fabs(opt.bench_cfg.min_total_time_s), opt.time_unit_mode);
    const TimeDisplaySpec max_total_debug_spec =
        pick_time_display_spec_from_s(std::fabs(opt.bench_cfg.max_total_time_s), opt.time_unit_mode);

    Table summary;
    summary.add_row(Row_t{"Benchmark", "Samples", fmt::format("Median ({}/op)", median_spec.suffix),
                          fmt::format("Mean ({}/op)", mean_spec.suffix), fmt::format("StdDev ({}/op)", stddev_spec.suffix),
                          fmt::format("P05 ({}/op)", p05_spec.suffix), fmt::format("P95 ({}/op)", p95_spec.suffix),
                          fmt::format("Min ({}/op)", min_spec.suffix), fmt::format("Max ({}/op)", max_spec.suffix),
                          fmt::format("Total ({})", total_spec.suffix), "Baseline Δ%", "Baseline SD Δ%"});
    summary[0].format().font_align(FontAlign::center);
    for (std::size_t col = 1; col < 12; ++col) {
        summary.column(col).format().font_align(FontAlign::right);
    }

    for (const auto &row : rows) {
        if (!row.c)
            continue;
        const std::string suite(row.c->suite);
        const auto        base_med_it = baseline_median_ns.find(suite);
        const auto        base_sd_it  = baseline_stddev_ns.find(suite);
        const double      base_median = (base_med_it == baseline_median_ns.end()) ? 0.0 : base_med_it->second;
        const double      base_sd     = (base_sd_it == baseline_stddev_ns.end()) ? 0.0 : base_sd_it->second;
        const std::string baseline_med_cell = (base_median > 0.0)
                                                  ? fmt::format("{:+.2f}%", (row.result.median_ns - base_median) / base_median * 100.0)
                                                  : std::string("-");
        const std::string baseline_sd_cell =
            (base_sd > 0.0) ? fmt::format("{:+.2f}%", (row.result.stddev_ns - base_sd) / base_sd * 100.0) : std::string("-");
        summary.add_row(Row_t{
            std::string(row.c->name),
            fmt::format("{}", row.result.samples_ns.size()),
            format_scaled_time_ns(row.result.median_ns, median_spec),
            format_scaled_time_ns(row.result.mean_ns, mean_spec),
            format_scaled_time_ns(row.result.stddev_ns, stddev_spec),
            format_scaled_time_ns(row.result.p05_ns, p05_spec),
            format_scaled_time_ns(row.result.p95_ns, p95_spec),
            format_scaled_time_ns(row.result.min_ns, min_spec),
            format_scaled_time_ns(row.result.max_ns, max_spec),
            format_scaled_time_s(row.result.wall_time_s, total_spec),
            baseline_med_cell,
            baseline_sd_cell,
        });
    }

    std::cout << "Jitter summary\n" << summary << "\n";

    Table debug;
    debug.add_row(Row_t{"Benchmark", "Mode", "Samples", "Iters/epoch", fmt::format("Overhead ({}/iter)", overhead_spec.suffix),
                        "Overhead %", fmt::format("Measured ({})", measured_debug_spec.suffix),
                        fmt::format("Warmup ({})", warmup_debug_spec.suffix), fmt::format("Min total ({})", min_total_debug_spec.suffix),
                        fmt::format("Max total ({})", max_total_debug_spec.suffix), fmt::format("Wall ({})", wall_debug_spec.suffix)});
    debug[0].format().font_align(FontAlign::center);
    for (std::size_t col = 2; col < 11; ++col) {
        debug.column(col).format().font_align(FontAlign::right);
    }

    for (const auto &row : rows) {
        if (!row.c)
            continue;
        const std::string mode          = row.result.batch_mode ? "batch" : "per-iter";
        const std::string overhead_cell = (row.result.overhead_mean_ns > 0.0)
                                              ? fmt::format("{} ± {}", format_scaled_time_ns(row.result.overhead_mean_ns, overhead_spec),
                                                            format_scaled_time_ns(row.result.overhead_sd_ns, overhead_spec))
                                              : std::string("-");
        const std::string overhead_pct =
            (row.result.overhead_ratio_pct > 0.0) ? fmt::format("{:.2f}%", row.result.overhead_ratio_pct) : std::string("-");
        debug.add_row(Row_t{
            std::string(row.c->name),
            mode,
            fmt::format("{}", row.result.samples_ns.size()),
            fmt::format("{}", row.result.iters_per_epoch),
            overhead_cell,
            overhead_pct,
            format_scaled_time_s(row.result.total_time_s, measured_debug_spec),
            format_scaled_time_s(row.result.warmup_time_s, warmup_debug_spec),
            format_scaled_time_s(opt.bench_cfg.min_total_time_s, min_total_debug_spec),
            format_scaled_time_s(opt.bench_cfg.max_total_time_s, max_total_debug_spec),
            format_scaled_time_s(row.result.wall_time_s, wall_debug_spec),
        });
    }

    std::cout << "Jitter debug\n" << debug << "\n";

    for (const auto &row : rows) {
        if (!row.c)
            continue;
        const auto &samples = row.result.samples_ns;
        std::cout << "\nJitter histogram (bins=" << bins << ", name=" << row.c->name << ")\n";
        const auto hist_data = gentest::detail::compute_histogram(samples, bins);

        double hist_abs_max_ns = 0.0;
        for (double sample_ns : samples) {
            hist_abs_max_ns = std::max(hist_abs_max_ns, std::fabs(sample_ns));
        }
        TimeDisplaySpec                  hist_spec    = pick_time_display_spec_from_ns(hist_abs_max_ns, opt.time_unit_mode);
        std::vector<DisplayHistogramBin> display_bins = make_display_histogram_bins(hist_data.bins, hist_spec);
        if (opt.time_unit_mode == TimeUnitMode::Auto) {
            while (has_duplicate_display_ranges(display_bins)) {
                TimeDisplaySpec finer_spec;
                if (!pick_finer_time_display_spec(hist_spec, finer_spec))
                    break;
                hist_spec    = finer_spec;
                display_bins = make_display_histogram_bins(hist_data.bins, hist_spec);
            }
        }
        const std::size_t pre_merge_bins = display_bins.size();
        if (has_duplicate_display_ranges(display_bins)) {
            display_bins = merge_duplicate_display_ranges(display_bins);
        }
        if (display_bins.size() < pre_merge_bins) {
            fmt::print("note: merged {} histogram bins due displayed {} range precision\n", pre_merge_bins - display_bins.size(),
                       hist_spec.suffix);
        }

        Table hist;
        hist.add_row(Row_t{"Bin", fmt::format("Range ({}/op)", hist_spec.suffix), "Count", "Percent", "Cumulative %"});
        hist[0].format().font_align(FontAlign::center);
        hist.column(0).format().font_align(FontAlign::right);
        hist.column(2).format().font_align(FontAlign::right);
        hist.column(3).format().font_align(FontAlign::right);
        hist.column(4).format().font_align(FontAlign::right);

        if (samples.empty()) {
            std::cout << hist << "\n";
            continue;
        }

        const auto  total_samples    = static_cast<double>(samples.size());
        std::size_t cumulative_count = 0;
        for (std::size_t i = 0; i < display_bins.size(); ++i) {
            const auto       &bin = display_bins[i];
            const std::string range =
                bin.inclusive_hi ? fmt::format("[{}, {}]", bin.lo_text, bin.hi_text) : fmt::format("[{}, {})", bin.lo_text, bin.hi_text);
            cumulative_count += bin.count;
            const double pct            = (total_samples > 0.0) ? (static_cast<double>(bin.count) / total_samples * 100.0) : 0.0;
            const double cumulative_pct = (total_samples > 0.0) ? (static_cast<double>(cumulative_count) / total_samples * 100.0) : 0.0;
            hist.add_row(Row_t{
                fmt::format("{}", i + 1),
                range,
                fmt::format("{}", bin.count),
                fmt::format("{:.2f}%", pct),
                fmt::format("{:.2f}%", cumulative_pct),
            });
        }

        std::cout << hist << "\n";
    }
}

} // namespace gentest::runner
