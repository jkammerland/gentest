#include "../../src/runner_measured_report.h"
#include "gentest/detail/bench_stats.h"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using gentest::Case;
using gentest::FixtureLifetime;
using gentest::detail::compute_histogram;
using gentest::detail::compute_sample_stats;
using gentest::runner::BenchReportRow;
using gentest::runner::BenchResult;
using gentest::runner::CliOptions;
using gentest::runner::JitterReportRow;
using gentest::runner::JitterResult;
using gentest::runner::ReportAttachment;
using gentest::runner::TimeUnitMode;

Case make_case(std::string_view name, std::string_view suite, bool is_benchmark, bool is_jitter, bool is_baseline) {
    return Case{
        .name             = name,
        .fn               = nullptr,
        .file             = __FILE__,
        .line             = __LINE__,
        .is_benchmark     = is_benchmark,
        .is_jitter        = is_jitter,
        .is_baseline      = is_baseline,
        .tags             = {},
        .requirements     = {},
        .skip_reason      = {},
        .should_skip      = false,
        .fixture          = {},
        .fixture_lifetime = FixtureLifetime::None,
        .suite            = suite,
    };
}

BenchResult make_bench_result(double median_ns, double mean_ns, double total_time_s, std::size_t total_iters) {
    return BenchResult{
        .epochs             = 3,
        .iters_per_epoch    = (total_iters == 0) ? 0 : (total_iters / 3),
        .total_iters        = total_iters,
        .best_ns            = std::max(0.0, median_ns - 2.0),
        .worst_ns           = median_ns + 4.0,
        .median_ns          = median_ns,
        .mean_ns            = mean_ns,
        .p05_ns             = std::max(0.0, median_ns - 1.0),
        .p95_ns             = median_ns + 2.0,
        .total_time_s       = total_time_s,
        .warmup_time_s      = 0.0002,
        .wall_time_s        = total_time_s + 0.0003,
        .calibration_time_s = 0.0001,
        .calibration_iters  = 8,
    };
}

JitterResult make_jitter_result(std::vector<double> samples, int stored_bins) {
    JitterResult result{};
    result.epochs             = 2;
    result.iters_per_epoch    = samples.empty() ? 0 : samples.size();
    result.total_iters        = samples.size();
    result.batch_mode         = false;
    result.total_time_s       = 0.001;
    result.warmup_time_s      = 0.0;
    result.wall_time_s        = 0.0012;
    result.calibration_time_s = 0.0001;
    result.calibration_iters  = 4;
    result.histogram_bins     = stored_bins;
    result.samples_ns         = std::move(samples);
    const auto stats          = compute_sample_stats(result.samples_ns);
    result.min_ns             = stats.min;
    result.max_ns             = stats.max;
    result.median_ns          = stats.median;
    result.mean_ns            = stats.mean;
    result.stddev_ns          = stats.stddev;
    result.p05_ns             = stats.p05;
    result.p95_ns             = stats.p95;
    result.histogram          = compute_histogram(result.samples_ns, stored_bins);
    return result;
}

bool contains(std::string_view haystack, std::string_view needle) { return haystack.find(needle) != std::string_view::npos; }

const ReportAttachment &find_attachment(const std::vector<ReportAttachment> &attachments, std::string_view name) {
    const auto it = std::find_if(attachments.begin(), attachments.end(),
                                 [name](const ReportAttachment &attachment) { return attachment.name == name; });
    if (it == attachments.end()) {
        throw std::runtime_error("missing attachment: " + std::string(name));
    }
    return *it;
}

std::string capture_stdout(const std::function<void()> &fn) {
    std::ostringstream buffer;
    auto              *old = std::cout.rdbuf(buffer.rdbuf());
    try {
        fn();
    } catch (...) {
        std::cout.rdbuf(old);
        throw;
    }
    std::cout.rdbuf(old);
    return buffer.str();
}

std::string_view line_containing(std::string_view text, std::string_view needle) {
    const auto pos = text.find(needle);
    if (pos == std::string_view::npos) {
        throw std::runtime_error("missing line for token: " + std::string(needle));
    }
    const auto start = text.rfind('\n', pos);
    const auto end   = text.find('\n', pos);
    return text.substr((start == std::string_view::npos) ? 0 : (start + 1),
                       (end == std::string_view::npos) ? std::string_view::npos
                                                       : (end - ((start == std::string_view::npos) ? 0 : (start + 1))));
}

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void check_bench_attachments() {
    const auto baseline_case = make_case("regressions/measured_report/bench_baseline", "measured_suite", true, false, true);
    const auto delta_case    = make_case("regressions/measured_report/bench_zero_calls", "other_suite", true, false, false);

    const auto baseline_attachments =
        gentest::runner::make_bench_allure_attachments(baseline_case, make_bench_result(10.0, 11.0, 0.015, 300));
    expect(baseline_attachments.size() == 2, "expected two bench attachments");
    expect(contains(find_attachment(baseline_attachments, "metrics").contents, "calls_per_sec\t20000"),
           "bench metrics should include non-zero calls/sec");
    expect(contains(find_attachment(baseline_attachments, "metrics").contents, "is_baseline\ttrue"),
           "bench metrics should record baseline status");
    expect(contains(find_attachment(baseline_attachments, "summary-plot").contents, "<svg"), "bench summary plot should be SVG");

    const auto zero_attachments = gentest::runner::make_bench_allure_attachments(delta_case, make_bench_result(25.0, 26.0, 0.0, 0));
    expect(contains(find_attachment(zero_attachments, "metrics").contents, "calls_per_sec\t0"),
           "bench metrics should report zero calls/sec when total_time_s is zero");
}

void check_jitter_histogram_recompute_and_truncation() {
    std::vector<double> samples(2050);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        samples[i] = static_cast<double>(i);
    }

    auto jitter       = make_jitter_result(std::move(samples), 4);
    jitter.batch_mode = true;

    const auto  jitter_case  = make_case("regressions/measured_report/jitter_large", "measured_suite", false, true, false);
    const auto  attachments  = gentest::runner::make_jitter_allure_attachments(jitter_case, jitter, 7);
    const auto &histogram    = find_attachment(attachments, "histogram").contents;
    const auto &histogramSvg = find_attachment(attachments, "histogram-plot").contents;
    const auto &samplesJson  = find_attachment(attachments, "samples").contents;

    expect(attachments.size() == 4, "expected four jitter attachments");
    expect(std::count(histogram.begin(), histogram.end(), '\n') == 8, "histogram TSV should contain seven recomputed bins plus header");
    expect(contains(histogramSvg, "bins 7  peak count"), "histogram SVG should reflect recomputed bin count");
    expect(contains(samplesJson, "\"sample_count\":2050"), "samples JSON should report original sample count");
    expect(contains(samplesJson, "\"stored_count\":2048"), "samples JSON should cap stored samples at 2048");
    expect(contains(samplesJson, "\"truncated\":true"), "samples JSON should mark truncation");
    expect(contains(samplesJson, "\"samples_ns\":[0,"), "samples JSON should keep the first sample");
    expect(contains(samplesJson, ",2049]"), "samples JSON should keep the last sample");
}

void check_zero_and_one_sample_attachments() {
    const auto jitter_case = make_case("regressions/measured_report/jitter_zero", "edge_suite", false, true, false);

    const auto zero = gentest::runner::make_jitter_allure_attachments(jitter_case, make_jitter_result({}, 1), 3);
    expect(contains(find_attachment(zero, "histogram").contents,
                    "bin\trange_lo_ns\trange_hi_ns\tinclusive_hi\tcount\tpercent\tcumulative_percent\n"),
           "zero-sample histogram should still emit header");
    expect(contains(find_attachment(zero, "histogram-plot").contents, "bins 0  peak count 1"),
           "zero-sample histogram SVG should handle empty bins");
    expect(
        contains(find_attachment(zero, "samples").contents, "\"sample_count\":0,\"stored_count\":0,\"truncated\":false,\"samples_ns\":[]"),
        "zero-sample JSON should stay empty without truncation");

    auto one                   = make_jitter_result({42.5}, 1);
    one.overhead_mean_ns       = 0.0;
    const auto one_attachments = gentest::runner::make_jitter_allure_attachments(
        make_case("regressions/measured_report/jitter_one", "edge_suite", false, true, true), one, 1);
    expect(contains(find_attachment(one_attachments, "histogram").contents, "1\t42.5\t42.5\ttrue\t1\t100\t100"),
           "single-sample histogram should preserve the lone sample");
    expect(contains(find_attachment(one_attachments, "samples").contents,
                    "\"sample_count\":1,\"stored_count\":1,\"truncated\":false,\"samples_ns\":[42.5]"),
           "single-sample JSON should keep the single sample");
}

void check_mixed_baseline_output() {
    const auto bench_baseline = make_case("regressions/measured_report/bench_baseline_row", "suite_a", true, false, true);
    const auto bench_delta    = make_case("regressions/measured_report/bench_delta_row", "suite_a", true, false, false);
    const auto bench_nobase   = make_case("regressions/measured_report/bench_nobase_row", "suite_b", true, false, false);

    std::vector<BenchReportRow> bench_rows{
        BenchReportRow{.c = &bench_baseline, .result = make_bench_result(10.0, 11.0, 0.015, 300)},
        BenchReportRow{.c = &bench_delta, .result = make_bench_result(15.0, 16.0, 0.012, 180)},
        BenchReportRow{.c = &bench_nobase, .result = make_bench_result(20.0, 21.0, 0.0, 0)},
    };

    CliOptions bench_opt{};
    bench_opt.time_unit_mode = TimeUnitMode::Ns;

    const std::string bench_output = capture_stdout([&] { gentest::runner::print_bench_report(bench_rows, bench_opt); });
    expect(contains(bench_output, "Baseline"), "bench report should render baseline column");
    expect(contains(bench_output, "+50.00%"), "bench report should show baseline delta for suite rows");
    expect(contains(line_containing(bench_output, "bench_nobase_row"), "-"),
           "bench report should show '-' when no baseline exists for the suite");

    auto jitter_empty_baseline      = make_jitter_result({}, 2);
    jitter_empty_baseline.median_ns = 8.0;
    jitter_empty_baseline.mean_ns   = 8.0;
    jitter_empty_baseline.stddev_ns = 2.0;
    jitter_empty_baseline.p05_ns    = 8.0;
    jitter_empty_baseline.p95_ns    = 8.0;
    jitter_empty_baseline.min_ns    = 8.0;
    jitter_empty_baseline.max_ns    = 8.0;

    auto jitter_delta               = make_jitter_result({8.0, 9.0, 11.0, 12.0}, 2);
    jitter_delta.batch_mode         = true;
    jitter_delta.median_ns          = 10.0;
    jitter_delta.mean_ns            = 10.0;
    jitter_delta.stddev_ns          = 4.0;
    jitter_delta.overhead_mean_ns   = 1.5;
    jitter_delta.overhead_sd_ns     = 0.5;
    jitter_delta.overhead_ratio_pct = 7.5;

    auto jitter_nobase             = make_jitter_result({30.0}, 1);
    jitter_nobase.overhead_mean_ns = 0.0;

    const auto jitter_baseline_case = make_case("regressions/measured_report/jitter_empty_baseline", "suite_a", false, true, true);
    const auto jitter_delta_case    = make_case("regressions/measured_report/jitter_delta_row", "suite_a", false, true, false);
    const auto jitter_nobase_case   = make_case("regressions/measured_report/jitter_nobase_row", "suite_b", false, true, false);

    std::vector<JitterReportRow> jitter_rows{
        JitterReportRow{.c = &jitter_baseline_case, .result = std::move(jitter_empty_baseline)},
        JitterReportRow{.c = &jitter_delta_case, .result = std::move(jitter_delta)},
        JitterReportRow{.c = &jitter_nobase_case, .result = std::move(jitter_nobase)},
    };

    CliOptions jitter_opt{};
    jitter_opt.time_unit_mode = TimeUnitMode::Ns;
    jitter_opt.jitter_bins    = 4;

    const std::string jitter_output = capture_stdout([&] { gentest::runner::print_jitter_report(jitter_rows, jitter_opt); });
    expect(contains(jitter_output, "Baseline SD"), "jitter report should render baseline stddev column");
    expect(contains(jitter_output, "+25.00%"), "jitter report should show median delta against baseline");
    expect(contains(jitter_output, "+100.00%"), "jitter report should show stddev delta against baseline");
    expect(contains(jitter_output, "batch"), "jitter debug table should report batch mode");
    expect(contains(line_containing(jitter_output, "jitter_nobase_row"), "-"),
           "jitter report should show '-' when suite baseline data is missing");
    expect(contains(jitter_output, "Jitter histogram (bins=4, name=regressions/measured_report/jitter_empty_baseline)"),
           "jitter report should print histogram header for empty samples");
}

} // namespace

int main() {
    try {
        check_bench_attachments();
        check_jitter_histogram_recompute_and_truncation();
        check_zero_and_one_sample_attachments();
        check_mixed_baseline_output();
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }

    return 0;
}
