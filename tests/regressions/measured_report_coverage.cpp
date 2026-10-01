#include "../../src/runner_measured_report.h"
#include "gentest/detail/bench_stats.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
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

Case make_case(std::string_view name, std::string_view suite, bool is_benchmark, bool is_jitter, bool is_baseline,
               std::uint64_t items_per_call = 1) {
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
        .items_per_call   = items_per_call,
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
    const auto it = std::ranges::find_if(attachments, [name](const ReportAttachment &attachment) { return attachment.name == name; });
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
    const auto baseline_case = make_case("regressions/measured_report/bench_baseline", "measured_suite", true, false, true, 2);
    const auto delta_case    = make_case("regressions/measured_report/bench_zero_calls", "other_suite", true, false, false);

    const auto baseline_attachments =
        gentest::runner::make_bench_allure_attachments(baseline_case, make_bench_result(10.0, 11.0, 0.015, 300));
    expect(baseline_attachments.size() == 2, "expected two bench attachments");
    expect(contains(*find_attachment(baseline_attachments, "metrics").contents, "calls_per_sec\t20000"),
           "bench metrics should include non-zero calls/sec");
    expect(contains(*find_attachment(baseline_attachments, "metrics").contents, "items_per_call\t2"),
           "bench metrics should include item count");
    expect(contains(*find_attachment(baseline_attachments, "metrics").contents, "items_per_sec\t40000"),
           "bench metrics should include item throughput");
    expect(contains(*find_attachment(baseline_attachments, "metrics").contents, "median_ns_per_item\t5"),
           "bench metrics should normalize item timing");
    expect(contains(*find_attachment(baseline_attachments, "metrics").contents, "is_baseline\ttrue"),
           "bench metrics should record baseline status");
    expect(contains(*find_attachment(baseline_attachments, "summary-plot").contents, "<svg"), "bench summary plot should be SVG");

    const auto zero_attachments = gentest::runner::make_bench_allure_attachments(delta_case, make_bench_result(25.0, 26.0, 0.0, 0));
    expect(contains(*find_attachment(zero_attachments, "metrics").contents, "calls_per_sec\t0"),
           "bench metrics should report zero calls/sec when total_time_s is zero");
}

void check_jitter_histogram_recompute_and_truncation() {
    std::vector<double> samples(2050);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        samples[i] = static_cast<double>(i);
    }

    auto jitter             = make_jitter_result(std::move(samples), 4);
    jitter.batch_mode       = true;
    jitter.calls_per_sample = 64;

    const auto  jitter_case  = make_case("regressions/measured_report/jitter_large", "measured_suite", false, true, false, 4);
    const auto  attachments  = gentest::runner::make_jitter_allure_attachments(jitter_case, jitter, 7);
    const auto &metrics      = *find_attachment(attachments, "metrics").contents;
    const auto &histogram    = *find_attachment(attachments, "histogram").contents;
    const auto &histogramSvg = *find_attachment(attachments, "histogram-plot").contents;
    const auto &samplesJson  = *find_attachment(attachments, "samples").contents;

    expect(attachments.size() == 4, "expected four jitter attachments");
    expect(contains(metrics, "items_per_call\t4"), "jitter metrics should include item count");
    expect(contains(metrics, "median_ns_per_item\t256.125"), "jitter metrics should normalize item timing");
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
    expect(contains(*find_attachment(zero, "histogram").contents,
                    "bin\trange_lo_ns\trange_hi_ns\tinclusive_hi\tcount\tpercent\tcumulative_percent\n"),
           "zero-sample histogram should still emit header");
    expect(contains(*find_attachment(zero, "histogram-plot").contents, "bins 0  peak count 1"),
           "zero-sample histogram SVG should handle empty bins");
    expect(contains(*find_attachment(zero, "samples").contents, R"("sample_count":0,"stored_count":0,"truncated":false,"samples_ns":[])"),
           "zero-sample JSON should stay empty without truncation");

    auto one                   = make_jitter_result({42.5}, 1);
    one.overhead_mean_ns       = 0.0;
    const auto one_attachments = gentest::runner::make_jitter_allure_attachments(
        make_case("regressions/measured_report/jitter_one", "edge_suite", false, true, true), one, 1);
    expect(contains(*find_attachment(one_attachments, "histogram").contents, "1\t42.5\t42.5\ttrue\t1\t100\t100"),
           "single-sample histogram should preserve the lone sample");
    expect(contains(*find_attachment(one_attachments, "samples").contents,
                    R"("sample_count":1,"stored_count":1,"truncated":false,"samples_ns":[42.5])"),
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
    jitter_delta.calls_per_sample   = 64;
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

void check_measured_report_formats_and_items() {
    const auto fast_case   = make_case("regressions/measured_report/fast_item_row", "suite_auto", true, false, true, 4);
    const auto slow_case   = make_case("regressions/measured_report/slow_item_row", "suite_auto", true, false, false, 2);
    const auto jitter_case = make_case("regressions/measured_report/jitter_item_row", "suite_auto", false, true, false, 5);

    std::vector<BenchReportRow> bench_rows{
        BenchReportRow{.c = &fast_case, .result = make_bench_result(20.0, 24.0, 0.010, 100)},
        BenchReportRow{.c = &slow_case, .result = make_bench_result(4'000'000.0, 4'200'000.0, 0.020, 10)},
    };

    CliOptions        auto_opt{};
    const std::string auto_output = capture_stdout([&] { gentest::runner::print_bench_report(bench_rows, auto_opt); });
    expect(contains(auto_output, "Items/call"), "bench report should show item metadata column");
    expect(contains(line_containing(auto_output, "fast_item_row"), "5 ns"), "auto unit report should preserve ns-scale rows");
    expect(contains(line_containing(auto_output, "slow_item_row"), "2.000 ms"), "auto unit report should scale slow rows independently");

    CliOptions markdown_opt{};
    markdown_opt.measured_report_format = gentest::runner::MeasuredReportFormat::Markdown;
    const std::string markdown_output   = capture_stdout([&] { gentest::runner::print_bench_report(bench_rows, markdown_opt); });
    expect(contains(markdown_output, "## Benchmarks"), "markdown bench output should include a heading");
    expect(contains(markdown_output, "| Benchmark | Samples |"), "markdown bench output should include a pipe table");

    CliOptions csv_opt{};
    csv_opt.measured_report_format = gentest::runner::MeasuredReportFormat::Csv;
    const std::string csv_output   = capture_stdout([&] { gentest::runner::print_bench_report(bench_rows, csv_opt); });
    expect(contains(csv_output, "report,table,row,field,type,value\n"), "csv bench output should include the long-form csv header");
    expect(contains(csv_output, "bench,bench.summary,0,items_per_call,number,4"),
           "csv bench output should include stable item count fields");

    CliOptions json_opt{};
    json_opt.measured_report_format = gentest::runner::MeasuredReportFormat::Json;
    const std::string json_output   = capture_stdout([&] { gentest::runner::print_bench_report(bench_rows, json_opt); });
    expect(contains(json_output, R"("report":"bench")"), "json bench output should include report kind");
    expect(contains(json_output, R"("items_per_call":4)"), "json bench output should include typed item count");
    expect(contains(json_output, R"("median_ns_per_item":5)"), "json bench output should include typed per-item timing");

    auto                         jitter = make_jitter_result({50.0, 55.0, 60.0, 65.0}, 2);
    std::vector<JitterReportRow> jitter_rows{
        JitterReportRow{.c = &jitter_case, .result = std::move(jitter)},
    };

    CliOptions jitter_markdown_opt{};
    jitter_markdown_opt.jitter_bins            = 2;
    jitter_markdown_opt.measured_report_format = gentest::runner::MeasuredReportFormat::Markdown;
    const std::string jitter_markdown_output =
        capture_stdout([&] { gentest::runner::print_jitter_report(jitter_rows, jitter_markdown_opt); });
    expect(contains(jitter_markdown_output, "## Jitter summary"), "markdown jitter output should include summary heading");
    expect(contains(jitter_markdown_output, "Range (ns/item)"), "jitter histogram should report per-item ranges");
    expect(contains(line_containing(jitter_markdown_output, "jitter_item_row"), "| 5 |"),
           "markdown jitter output should include item count");

    const auto jitter_title_case = make_case("regressions/measured_report/jitter_title|\n# injected", "suite_auto", false, true, false);
    auto       jitter_title      = make_jitter_result({70.0, 75.0}, 2);
    std::vector<JitterReportRow> jitter_title_rows{
        JitterReportRow{.c = &jitter_title_case, .result = std::move(jitter_title)},
    };
    const std::string escaped_title_markdown =
        capture_stdout([&] { gentest::runner::print_jitter_report(jitter_title_rows, jitter_markdown_opt); });
    expect(contains(escaped_title_markdown, R"(name=regressions/measured_report/jitter_title\|<br># injected)"),
           "markdown jitter histogram headings should escape case names");
    expect(!contains(escaped_title_markdown, "\n# injected"), "markdown jitter histogram headings should not inject new headings");

    const std::string measured_json_output =
        capture_stdout([&] { gentest::runner::print_measured_report(bench_rows, jitter_rows, json_opt); });
    expect(contains(measured_json_output, R"("report":"measured")"), "combined json output should use one measured root");
    expect(std::count(measured_json_output.begin(), measured_json_output.end(), '\n') == 1,
           "combined json output should be one newline-terminated document");
    expect(contains(measured_json_output, R"("id":"bench.summary")"), "combined json output should include bench tables");
    expect(contains(measured_json_output, R"("id":"jitter.summary")"), "combined json output should include jitter tables");

    const auto escaped_case = make_case("regressions/measured_report/pipe|quote\"comma,\nline", "suite_escape", true, false, false);
    std::vector<BenchReportRow> escaped_rows{
        BenchReportRow{.c = &escaped_case, .result = make_bench_result(12.0, 12.0, 0.001, 3)},
    };
    const std::string escaped_markdown = capture_stdout([&] { gentest::runner::print_bench_report(escaped_rows, markdown_opt); });
    expect(contains(escaped_markdown, R"(pipe\|quote"comma,<br>line)"), "markdown output should escape pipes and newlines");
    const std::string escaped_csv = capture_stdout([&] { gentest::runner::print_bench_report(escaped_rows, csv_opt); });
    expect(contains(escaped_csv, "\"regressions/measured_report/pipe|quote\"\"comma,\nline\""),
           "csv output should quote commas, quotes, and newlines");
    const std::string escaped_json = capture_stdout([&] { gentest::runner::print_bench_report(escaped_rows, json_opt); });
    expect(contains(escaped_json, R"(pipe|quote\"comma,\nline)"), "json output should escape quotes and newlines");
}

void check_jitter_batch_reporting() {
    const auto jitter_case  = make_case("jitter/example", "jitter", false, true, false);
    auto       jitter       = make_jitter_result({10.0, 11.0, 12.0, 20.0}, 2);
    jitter.batch_mode       = true;
    jitter.calls_per_sample = 64;
    std::vector<JitterReportRow> rows{JitterReportRow{.c = &jitter_case, .result = jitter}};
    CliOptions                   opt{};
    opt.time_unit_mode        = TimeUnitMode::Ns;
    const std::string output  = capture_stdout([&] { gentest::runner::print_jitter_report(rows, opt); });
    const auto        summary = std::string_view(output).substr(0, output.find("Jitter debug"));
    expect(contains(summary, "Calls/sample"), "jitter summary must expose how many calls form a sample");
    expect(contains(summary, "batch-average"), "jitter summary must distinguish batch averages from individual calls");
    expect(contains(summary, "individual-call latency spikes may be hidden"),
           "batch averaging must warn about hidden individual-call tails");
    expect(contains(summary, "Max batch avg (ns/item)"), "batch-only summaries must label the maximum as a batch average");
    expect(contains(summary, "64 calls/sample"), "the warning must include the actual batch size");
    expect(output.find("WARNING:") != std::string::npos && output.find("WARNING:") == output.rfind("WARNING:"),
           "table reports must warn once for the affected case");
    expect(contains(output, "Jitter batch-average histogram (bins=10, name=jitter/example, calls/sample=64)"),
           "batch histograms must identify their sample type and batch size");

    opt.measured_report_format = gentest::runner::MeasuredReportFormat::Markdown;
    const std::string markdown = capture_stdout([&] { gentest::runner::print_jitter_report(rows, opt); });
    expect(contains(markdown, "> **Warning:** jitter/example:"), "markdown must display the batch warning");
    expect(contains(markdown, "| jitter/example | batch-average | 64 | 4 | 1 |"), "markdown summary must carry batch metadata");

    opt.measured_report_format     = gentest::runner::MeasuredReportFormat::Json;
    const std::string json         = capture_stdout([&] { gentest::runner::print_jitter_report(rows, opt); });
    const auto        json_summary = std::string_view(json).substr(0, json.find("jitter.debug"));
    expect(contains(json_summary, R"("mode":"batch","sample_kind":"batch-average","calls_per_sample":64)"),
           "JSON summaries must expose typed sampling metadata");
    expect(contains(json_summary, R"("sampling_warning":"jitter/example:)"), "JSON summaries must include a structured warning");
    expect(contains(json_summary, R"("max_ns_per_item":20)"), "existing machine timing fields must retain their values");
    expect(json.starts_with('{') && std::count(json.begin(), json.end(), '\n') == 1 && !contains(json, "WARNING:"),
           "batch warnings must not pollute JSON stdout");

    opt.measured_report_format = gentest::runner::MeasuredReportFormat::Csv;
    const std::string csv      = capture_stdout([&] { gentest::runner::print_jitter_report(rows, opt); });
    expect(csv.starts_with("report,table,row,field,type,value\n"), "batch warnings must preserve the CSV schema");
    expect(contains(csv, "jitter,jitter.summary,0,sample_kind,string,batch-average\n"), "CSV summaries must expose the sample kind");
    expect(contains(csv, "jitter,jitter.summary,0,calls_per_sample,number,64\n"), "CSV summaries must expose the batch size");
    expect(contains(csv, "jitter,jitter.summary,0,sampling_warning,string,\"jitter/example:"), "CSV warnings must be escaped fields");
    expect(!contains(csv, "WARNING:"), "batch warnings must not add prose records to CSV stdout");

    const auto  attachments = gentest::runner::make_jitter_allure_attachments(jitter_case, jitter, 2);
    const auto &metrics     = *find_attachment(attachments, "metrics").contents;
    const auto &samples     = *find_attachment(attachments, "samples").contents;
    expect(contains(metrics, "sample_kind\tbatch-average\ncalls_per_sample\t64\nsampling_warning\tjitter/example:"),
           "Allure metrics must include batch metadata and the warning");
    expect(contains(samples, R"("sample_kind":"batch-average","calls_per_sample":64,"sampling_warning":"jitter/example:)"),
           "raw samples must identify batch averages and include the warning");
    expect(contains(*find_attachment(attachments, "histogram-plot").contents, "batch-average jitter histogram (ns/call; 64 calls/sample)"),
           "Allure plots must identify batch averages");

    rows.push_back(JitterReportRow{.c = &jitter_case, .result = make_jitter_result({10.0, 20.0}, 2)});
    opt.measured_report_format = gentest::runner::MeasuredReportFormat::Markdown;
    const std::string mixed    = capture_stdout([&] { gentest::runner::print_jitter_report(rows, opt); });
    expect(contains(mixed, "Max sample (ns/item)"), "mixed sampling summaries must use a common sample maximum label");
    expect(contains(mixed, "| jitter/example | per-call | 1 | 2 | 1 |"), "per-call rows must display one call per sample");

    rows.resize(1);
    rows[0].result.calls_per_sample     = 1;
    const std::string single_call_batch = capture_stdout([&] { gentest::runner::print_jitter_report(rows, opt); });
    expect(!contains(single_call_batch, "**Warning:**") && !contains(single_call_batch, "batch-average"),
           "batches of one call must not warn about averaging or claim averaged samples");
    expect(contains(single_call_batch, "| jitter/example | per-call | 1 | 4 | 1 |"),
           "one-call batches must show per-call sample semantics");
    opt.measured_report_format         = gentest::runner::MeasuredReportFormat::Json;
    const std::string single_call_json = capture_stdout([&] { gentest::runner::print_jitter_report(rows, opt); });
    expect(contains(single_call_json, R"("mode":"batch","sample_kind":"per-call","calls_per_sample":1,"sampling_warning":null)"),
           "one-call batches must retain the executor mode while reporting no averaging warning");

    rows[0].result.batch_mode       = false;
    const auto per_call_attachments = gentest::runner::make_jitter_allure_attachments(jitter_case, rows[0].result, 2);
    expect(contains(*find_attachment(per_call_attachments, "samples").contents,
                    R"("mode":"per-call","sample_kind":"per-call","calls_per_sample":1,"sampling_warning":null)"),
           "per-call raw samples must carry explicit sample semantics without a warning");

    const auto escaped_case            = make_case("jitter/pipe|quote\"comma,\nline", "jitter", false, true, false);
    rows[0]                            = JitterReportRow{.c = &escaped_case, .result = jitter};
    opt.measured_report_format         = gentest::runner::MeasuredReportFormat::Markdown;
    const std::string escaped_markdown = capture_stdout([&] { gentest::runner::print_jitter_report(rows, opt); });
    expect(contains(escaped_markdown, R"(> **Warning:** jitter/pipe\|quote"comma,<br>line:)"), "markdown warnings must escape case names");
    const auto escaped_attachments = gentest::runner::make_jitter_allure_attachments(escaped_case, jitter, 2);
    expect(contains(*find_attachment(escaped_attachments, "samples").contents, R"(jitter/pipe|quote\"comma,\nline:)"),
           "raw sample warnings must JSON-escape case names");
}

void check_jitter_executor_sample_accounting() {
    auto jitter_case        = make_case("jitter/accounting", "jitter", false, true, false);
    jitter_case.fn          = [](void *) {};
    const std::size_t index = 0;
    CliOptions        opt{};
    opt.measured_report_format     = gentest::runner::MeasuredReportFormat::Json;
    opt.bench_cfg.min_epoch_time_s = 0.0001;
    opt.bench_cfg.max_total_time_s = 0.005;
    opt.bench_cfg.warmup_epochs    = 0;
    opt.bench_cfg.measure_epochs   = 1;
    std::vector<JitterReportRow> rows;
    const auto                   status = gentest::runner::run_selected_jitters(
        std::span<const Case>(&jitter_case, 1), std::span<const std::size_t>(&index, 1), opt, false,
        [](const Case &, const JitterResult &) {}, [](const Case &, const gentest::runner::MeasurementCaseFailure &, std::string_view) {},
        &rows);
    expect(status.ok && status.passed == 1 && rows.size() == 1, "the accounting jitter must produce one successful result");
    const auto &result = rows.front().result;
    expect(!result.samples_ns.empty() && result.calls_per_sample > 0, "successful jitter results must identify nonempty samples");
    expect(result.total_iters == result.samples_ns.size() * result.calls_per_sample,
           "reported calls per sample must account for the actual measured call count");
    expect(result.batch_mode || result.calls_per_sample == 1, "per-call execution must report exactly one call per sample");
}

} // namespace

int main() {
    try {
        check_bench_attachments();
        check_jitter_histogram_recompute_and_truncation();
        check_zero_and_one_sample_attachments();
        check_mixed_baseline_output();
        check_measured_report_formats_and_items();
        check_jitter_batch_reporting();
        check_jitter_executor_sample_accounting();
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }

    return 0;
}
