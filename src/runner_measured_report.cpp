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

std::string escape_tsv_cell(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
        case '\\': out += "\\\\"; break;
        case '\t': out += "\\t"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        default: out.push_back(ch); break;
        }
    }
    return out;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void append_tsv_metric(std::string &out, std::string_view key, std::string_view value) {
    out.append(escape_tsv_cell(key));
    out.push_back('\t');
    out.append(escape_tsv_cell(value));
    out.push_back('\n');
}

void append_tsv_metric(std::string &out, std::string_view key, double value) { append_tsv_metric(out, key, fmt::format("{}", value)); }

void append_tsv_metric(std::string &out, std::string_view key, std::size_t value) { append_tsv_metric(out, key, fmt::format("{}", value)); }

std::string escape_xml_text(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&apos;"; break;
        default: out.push_back(ch); break;
        }
    }
    return out;
}

double safe_axis_max(std::initializer_list<double> values) {
    double max_value = 0.0;
    for (double value : values) {
        max_value = std::max(max_value, value);
    }
    return (max_value > 0.0) ? max_value : 1.0;
}

double scale_x(double value, double axis_max, double left, double width) {
    const double clamped = std::clamp(value, 0.0, axis_max);
    return left + ((axis_max > 0.0) ? (clamped / axis_max) * width : 0.0);
}

double scale_y(double value, double axis_max, double top, double height) {
    const double clamped = std::clamp(value, 0.0, axis_max);
    return top + height - ((axis_max > 0.0) ? (clamped / axis_max) * height : 0.0);
}

std::string make_bench_summary_svg(const gentest::Case &c, const BenchResult &result) {
    constexpr double width        = 720.0;
    constexpr double height       = 180.0;
    constexpr double left_margin  = 64.0;
    constexpr double right_margin = 28.0;
    constexpr double top_margin   = 34.0;
    constexpr double plot_height  = 72.0;
    const double     plot_width   = width - left_margin - right_margin;
    const double     center_y     = top_margin + (plot_height / 2.0);
    const double     axis_max =
        safe_axis_max({result.best_ns, result.p05_ns, result.median_ns, result.mean_ns, result.p95_ns, result.worst_ns});

    const double best_x   = scale_x(result.best_ns, axis_max, left_margin, plot_width);
    const double p05_x    = scale_x(result.p05_ns, axis_max, left_margin, plot_width);
    const double median_x = scale_x(result.median_ns, axis_max, left_margin, plot_width);
    const double mean_x   = scale_x(result.mean_ns, axis_max, left_margin, plot_width);
    const double p95_x    = scale_x(result.p95_ns, axis_max, left_margin, plot_width);
    const double worst_x  = scale_x(result.worst_ns, axis_max, left_margin, plot_width);

    return fmt::format(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"{0}\" height=\"{1}\" viewBox=\"0 0 {0} {1}\">"
        "<rect width=\"100%\" height=\"100%\" fill=\"#ffffff\" stroke=\"#d0d7de\"/>"
        "<text x=\"{2}\" y=\"22\" font-family=\"monospace\" font-size=\"13\" fill=\"#111827\">{3}</text>"
        "<text x=\"{2}\" y=\"40\" font-family=\"monospace\" font-size=\"11\" fill=\"#4b5563\">ns/op summary</text>"
        "<line x1=\"{4:.2f}\" y1=\"{5:.2f}\" x2=\"{9:.2f}\" y2=\"{5:.2f}\" stroke=\"#9ca3af\" stroke-width=\"3\"/>"
        "<line x1=\"{6:.2f}\" y1=\"{5:.2f}\" x2=\"{8:.2f}\" y2=\"{5:.2f}\" stroke=\"#2563eb\" stroke-width=\"8\" stroke-linecap=\"round\"/>"
        "<line x1=\"{4:.2f}\" y1=\"{10:.2f}\" x2=\"{4:.2f}\" y2=\"{11:.2f}\" stroke=\"#6b7280\" stroke-width=\"2\"/>"
        "<line x1=\"{9:.2f}\" y1=\"{10:.2f}\" x2=\"{9:.2f}\" y2=\"{11:.2f}\" stroke=\"#6b7280\" stroke-width=\"2\"/>"
        "<line x1=\"{7:.2f}\" y1=\"{12:.2f}\" x2=\"{7:.2f}\" y2=\"{13:.2f}\" stroke=\"#111827\" stroke-width=\"3\"/>"
        "<circle cx=\"{14:.2f}\" cy=\"{5:.2f}\" r=\"5\" fill=\"#f97316\" stroke=\"#9a3412\" stroke-width=\"1\"/>"
        "<text x=\"{2}\" y=\"126\" font-family=\"monospace\" font-size=\"11\" fill=\"#374151\">best {15:.3f} ns</text>"
        "<text x=\"{2}\" y=\"142\" font-family=\"monospace\" font-size=\"11\" fill=\"#374151\">p05 {16:.3f}  median {17:.3f}  mean "
        "{18:.3f}</text>"
        "<text x=\"{2}\" y=\"158\" font-family=\"monospace\" font-size=\"11\" fill=\"#374151\">p95 {19:.3f}  worst {20:.3f}  epochs {21}  "
        "iters/epoch {22}</text>"
        "<text x=\"{23}\" y=\"174\" text-anchor=\"end\" font-family=\"monospace\" font-size=\"11\" fill=\"#6b7280\">axis max {24:.3f} "
        "ns/op</text>"
        "</svg>",
        width, height, left_margin, escape_xml_text(c.name), best_x, center_y, p05_x, median_x, p95_x, worst_x, center_y - 14.0,
        center_y + 14.0, center_y - 16.0, center_y + 16.0, mean_x, result.best_ns, result.p05_ns, result.median_ns, result.mean_ns,
        result.p95_ns, result.worst_ns, result.epochs, result.iters_per_epoch, width - right_margin, axis_max);
}

std::string make_jitter_histogram_svg(const gentest::Case &c, std::span<const gentest::detail::HistogramBin> bins) {
    constexpr double width         = 720.0;
    constexpr double height        = 220.0;
    constexpr double left_margin   = 56.0;
    constexpr double right_margin  = 28.0;
    constexpr double top_margin    = 34.0;
    constexpr double bottom_margin = 44.0;
    const double     plot_width    = width - left_margin - right_margin;
    const double     plot_height   = height - top_margin - bottom_margin;

    std::size_t max_count = 0;
    double      first_lo  = 0.0;
    double      last_hi   = 0.0;
    if (!bins.empty()) {
        first_lo = bins.front().lo;
        last_hi  = bins.back().hi;
    }
    for (const auto &bin : bins) {
        max_count = std::max(max_count, bin.count);
    }
    if (max_count == 0) {
        max_count = 1;
    }

    std::string  bars;
    const double slot_width = bins.empty() ? plot_width : (plot_width / static_cast<double>(bins.size()));
    for (std::size_t i = 0; i < bins.size(); ++i) {
        const auto  &bin     = bins[i];
        const double bar_gap = std::max(2.0, slot_width * 0.12);
        const double bar_x   = left_margin + (slot_width * static_cast<double>(i)) + (bar_gap / 2.0);
        const double bar_w   = std::max(1.0, slot_width - bar_gap);
        const double bar_h   = (static_cast<double>(bin.count) / static_cast<double>(max_count)) * plot_height;
        const double bar_y   = top_margin + plot_height - bar_h;
        bars += fmt::format(R"(<rect x="{:.2f}" y="{:.2f}" width="{:.2f}" height="{:.2f}" fill="#2563eb" opacity="0.82"/>)", bar_x, bar_y,
                            bar_w, bar_h);
    }

    return fmt::format(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"{0}\" height=\"{1}\" viewBox=\"0 0 {0} {1}\">"
        "<rect width=\"100%\" height=\"100%\" fill=\"#ffffff\" stroke=\"#d0d7de\"/>"
        "<text x=\"{2}\" y=\"22\" font-family=\"monospace\" font-size=\"13\" fill=\"#111827\">{3}</text>"
        "<text x=\"{2}\" y=\"40\" font-family=\"monospace\" font-size=\"11\" fill=\"#4b5563\">jitter histogram (ns/op)</text>"
        "<line x1=\"{2}\" y1=\"{4:.2f}\" x2=\"{5}\" y2=\"{4:.2f}\" stroke=\"#9ca3af\" stroke-width=\"1\"/>"
        "<line x1=\"{2}\" y1=\"{6}\" x2=\"{5}\" y2=\"{6}\" stroke=\"#111827\" stroke-width=\"1\"/>"
        "{7}"
        "<text x=\"{2}\" y=\"{8}\" font-family=\"monospace\" font-size=\"11\" fill=\"#374151\">min {9:.3f} ns</text>"
        "<text x=\"{5}\" y=\"{8}\" text-anchor=\"end\" font-family=\"monospace\" font-size=\"11\" fill=\"#374151\">max {10:.3f} ns</text>"
        "<text x=\"{2}\" y=\"{11}\" font-family=\"monospace\" font-size=\"11\" fill=\"#374151\">bins {12}  peak count {13}</text>"
        "</svg>",
        width, height, left_margin, escape_xml_text(c.name), top_margin, width - right_margin, height - bottom_margin, bars, height - 20.0,
        first_lo, last_hi, height - 6.0, bins.size(), max_count);
}

std::string make_samples_json(std::span<const double> samples_ns) {
    constexpr std::size_t kMaxStoredSamples = 2048;

    const std::size_t stored_count = std::min(samples_ns.size(), kMaxStoredSamples);
    std::string out = fmt::format(R"({{"sample_count":{},"stored_count":{},"truncated":{},"samples_ns":[)", samples_ns.size(), stored_count,
                                  (samples_ns.size() > stored_count) ? "true" : "false");
    if (stored_count == 0) {
        out += "]}";
        return out;
    }

    for (std::size_t i = 0; i < stored_count; ++i) {
        if (i != 0) {
            out.push_back(',');
        }

        const std::size_t sample_index = (stored_count == samples_ns.size()) ? i : ((i * (samples_ns.size() - 1)) / (stored_count - 1));
        out.append(fmt::format("{}", samples_ns[sample_index]));
    }
    out += "]}";
    return out;
}

const gentest::detail::Histogram &select_jitter_histogram(const JitterResult &result, int bins,
                                                          gentest::detail::Histogram &fallback_histogram) {
    if (result.histogram_bins == bins) {
        return result.histogram;
    }
    fallback_histogram = gentest::detail::compute_histogram(result.samples_ns, bins);
    return fallback_histogram;
}

} // namespace

std::vector<ReportAttachment> make_bench_allure_attachments(const gentest::Case &c, const BenchResult &result) {
    std::vector<ReportAttachment> attachments;

    std::string metrics = "metric\tvalue\n";
    append_tsv_metric(metrics, "name", std::string_view(c.name));
    append_tsv_metric(metrics, "suite", std::string_view(c.suite));
    append_tsv_metric(metrics, "is_baseline", c.is_baseline ? "true" : "false");
    append_tsv_metric(metrics, "epochs", result.epochs);
    append_tsv_metric(metrics, "iters_per_epoch", result.iters_per_epoch);
    append_tsv_metric(metrics, "total_iters", result.total_iters);
    append_tsv_metric(metrics, "best_ns_per_op", result.best_ns);
    append_tsv_metric(metrics, "median_ns_per_op", result.median_ns);
    append_tsv_metric(metrics, "mean_ns_per_op", result.mean_ns);
    append_tsv_metric(metrics, "p05_ns_per_op", result.p05_ns);
    append_tsv_metric(metrics, "p95_ns_per_op", result.p95_ns);
    append_tsv_metric(metrics, "worst_ns_per_op", result.worst_ns);
    append_tsv_metric(metrics, "total_time_s", result.total_time_s);
    append_tsv_metric(metrics, "warmup_time_s", result.warmup_time_s);
    append_tsv_metric(metrics, "wall_time_s", result.wall_time_s);
    append_tsv_metric(metrics, "calibration_time_s", result.calibration_time_s);
    append_tsv_metric(metrics, "calibration_iters", result.calibration_iters);
    const double calls_per_sec =
        (result.total_time_s > 0.0 && result.total_iters != 0) ? (static_cast<double>(result.total_iters) / result.total_time_s) : 0.0;
    append_tsv_metric(metrics, "calls_per_sec", calls_per_sec);

    attachments.push_back(ReportAttachment{
        .name           = "metrics",
        .mime_type      = "text/tab-separated-values",
        .file_extension = ".tsv",
        .contents       = std::move(metrics),
    });

    attachments.push_back(ReportAttachment{
        .name           = "summary-plot",
        .mime_type      = "image/svg+xml",
        .file_extension = ".svg",
        .contents       = make_bench_summary_svg(c, result),
    });

    return attachments;
}

std::vector<ReportAttachment> make_jitter_allure_attachments(const gentest::Case &c, const JitterResult &result, int bins) {
    std::vector<ReportAttachment> attachments;

    std::string metrics = "metric\tvalue\n";
    append_tsv_metric(metrics, "name", std::string_view(c.name));
    append_tsv_metric(metrics, "suite", std::string_view(c.suite));
    append_tsv_metric(metrics, "is_baseline", c.is_baseline ? "true" : "false");
    append_tsv_metric(metrics, "batch_mode", result.batch_mode ? "true" : "false");
    append_tsv_metric(metrics, "epochs", result.epochs);
    append_tsv_metric(metrics, "samples", result.samples_ns.size());
    append_tsv_metric(metrics, "iters_per_epoch", result.iters_per_epoch);
    append_tsv_metric(metrics, "total_iters", result.total_iters);
    append_tsv_metric(metrics, "min_ns_per_op", result.min_ns);
    append_tsv_metric(metrics, "max_ns_per_op", result.max_ns);
    append_tsv_metric(metrics, "median_ns_per_op", result.median_ns);
    append_tsv_metric(metrics, "mean_ns_per_op", result.mean_ns);
    append_tsv_metric(metrics, "stddev_ns_per_op", result.stddev_ns);
    append_tsv_metric(metrics, "p05_ns_per_op", result.p05_ns);
    append_tsv_metric(metrics, "p95_ns_per_op", result.p95_ns);
    append_tsv_metric(metrics, "overhead_mean_ns_per_iter", result.overhead_mean_ns);
    append_tsv_metric(metrics, "overhead_sd_ns_per_iter", result.overhead_sd_ns);
    append_tsv_metric(metrics, "overhead_ratio_pct", result.overhead_ratio_pct);
    append_tsv_metric(metrics, "total_time_s", result.total_time_s);
    append_tsv_metric(metrics, "warmup_time_s", result.warmup_time_s);
    append_tsv_metric(metrics, "wall_time_s", result.wall_time_s);
    append_tsv_metric(metrics, "calibration_time_s", result.calibration_time_s);
    append_tsv_metric(metrics, "calibration_iters", result.calibration_iters);

    attachments.push_back(ReportAttachment{
        .name           = "metrics",
        .mime_type      = "text/tab-separated-values",
        .file_extension = ".tsv",
        .contents       = std::move(metrics),
    });

    std::string                histogram = "bin\trange_lo_ns\trange_hi_ns\tinclusive_hi\tcount\tpercent\tcumulative_percent\n";
    gentest::detail::Histogram fallback_histogram;
    const auto                &hist = select_jitter_histogram(result, bins, fallback_histogram);
    for (std::size_t i = 0; i < hist.bins.size(); ++i) {
        const auto &bin = hist.bins[i];
        histogram.append(fmt::format("{}\t{}\t{}\t{}\t{}\t{}\t{}\n", i + 1, bin.lo, bin.hi, bin.inclusive_hi ? "true" : "false", bin.count,
                                     bin.percent, bin.cumulative_percent));
    }
    attachments.push_back(ReportAttachment{
        .name           = "histogram",
        .mime_type      = "text/tab-separated-values",
        .file_extension = ".tsv",
        .contents       = std::move(histogram),
    });

    attachments.push_back(ReportAttachment{
        .name           = "histogram-plot",
        .mime_type      = "image/svg+xml",
        .file_extension = ".svg",
        .contents       = make_jitter_histogram_svg(c, hist.bins),
    });

    attachments.push_back(ReportAttachment{
        .name           = "samples",
        .mime_type      = "application/json",
        .file_extension = ".json",
        .contents       = make_samples_json(result.samples_ns),
    });

    return attachments;
}

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
    const TimeDisplaySpec calib_debug_spec = pick_time_display_spec_from_s(
        bench_max_abs_s([](const BenchResult &result) { return result.calibration_time_s; }), opt.time_unit_mode);
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
    const int                     bins = opt.jitter_bins;
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
    const TimeDisplaySpec warmup_debug_spec = pick_time_display_spec_from_s(
        jitter_max_abs_s([](const JitterResult &result) { return result.warmup_time_s; }), opt.time_unit_mode);
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
        const std::string baseline_med_cell =
            (base_median > 0.0) ? fmt::format("{:+.2f}%", (row.result.median_ns - base_median) / base_median * 100.0) : std::string("-");
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
        gentest::detail::Histogram fallback_histogram;
        const auto                &hist_data = select_jitter_histogram(row.result, bins, fallback_histogram);

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
