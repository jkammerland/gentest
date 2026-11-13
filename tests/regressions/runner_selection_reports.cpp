// Regression checks for runner selection, CLI validation and report writers.
// Each mode is registered as its own CTest entry so one failure stays isolated.
#include "../../src/runner_cli.h"
#include "../../src/runner_measured_executor.h"
#include "../../src/runner_replay.h"
#include "../../src/runner_reporting.h"
#include "../../src/runner_selector.h"
#include "gentest/detail/generated_runtime.h"
#include "gentest/detail/runtime_support.h"
#include "gentest/runner.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <locale>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using gentest::Case;
using gentest::runner::CliOptions;

int g_failures = 0;

void expect(bool condition, std::string_view what) {
    if (!condition) {
        ++g_failures;
        std::cerr << "FAILED: " << what << '\n';
    }
}

void noop_case(void *) {}

std::string read_file(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

Case make_case(std::string_view name) {
    return Case{
        .name             = name,
        .fn               = &noop_case,
        .file             = __FILE__,
        .line             = __LINE__,
        .is_benchmark     = false,
        .is_jitter        = false,
        .is_baseline      = false,
        .tags             = {},
        .requirements     = {},
        .skip_reason      = {},
        .should_skip      = false,
        .fixture          = {},
        .fixture_lifetime = gentest::FixtureLifetime::None,
        .suite            = "regressions",
    };
}

std::vector<std::string> selected_names(std::span<const Case> cases, const CliOptions &options) {
    std::vector<std::string> names;
    const auto               selection = gentest::runner::select_cases(cases, options);
    names.reserve(selection.idxs.size());
    for (const auto index : selection.idxs)
        names.emplace_back(cases[index].name);
    return names;
}

// A pattern '*' that lines up with a literal '*' in the name is still a wildcard.
int check_wildcard_literal_star() {
    const std::array cases{make_case("*ab"), make_case("ptr/deref<int*>"), make_case("ptr/deref<long>"), make_case("a*b*c")};
    const auto       filter = [&](const char *pattern) {
        CliOptions options;
        options.filter_pat = pattern;
        return selected_names(cases, options);
    };
    using Names = std::vector<std::string>;
    expect(filter("*b") == Names{"*ab"}, "--filter='*b' selects '*ab'");
    expect(filter("*int*") == Names{"ptr/deref<int*>"}, "--filter='*int*' selects 'ptr/deref<int*>'");
    expect(filter("*c") == Names{"a*b*c"}, "--filter='*c' selects 'a*b*c'");
    expect(filter("a*c") == Names{"a*b*c"}, "--filter='a*c' selects 'a*b*c'");
    expect(filter("*") == Names{"*ab", "ptr/deref<int*>", "ptr/deref<long>", "a*b*c"}, "--filter='*' selects everything");
    expect(filter("?ab") == Names{"*ab"}, "'?' still matches a literal '*'");
    expect(filter("*x").empty(), "a non-matching pattern stays non-matching");

    CliOptions excluded;
    excluded.exclude_patterns = {"*int*", "*b"};
    expect(selected_names(cases, excluded) == Names{"ptr/deref<long>", "a*b*c"}, "--exclude honours the same wildcard rule");
    return g_failures == 0 ? 0 : 1;
}

std::size_t g_bench_calls = 0;

void counting_bench(void *) {
    if (gentest::detail::bench_phase() == gentest::detail::BenchPhase::Call)
        ++g_bench_calls;
}

// Calibration that never reaches the minimum epoch time stops at the cap and
// hands back the last count it executed, not the next untried doubling.
int check_calibration_cap() {
    Case bench         = make_case("regressions/calibration_cap/bench");
    bench.fn           = &counting_bench;
    bench.is_benchmark = true;
    const std::array                 cases{bench};
    const std::array<std::size_t, 1> indices{0};

    CliOptions options;
    options.bench_cfg.min_epoch_time_s           = 1.0e9; // never satisfied
    options.bench_cfg.min_total_time_s           = 0.0;
    options.bench_cfg.warmup_epochs              = 0;
    options.bench_cfg.measure_epochs             = 1;
    options.bench_cfg.max_calibration_iterations = 8;

    std::size_t                  successes = 0;
    std::size_t                  failures  = 0;
    gentest::runner::BenchResult result;
    const auto                   status = gentest::runner::run_selected_benches(
        cases, indices, options, false,
        [&](const Case &, const gentest::runner::BenchResult &measured) {
            ++successes;
            result = measured;
        },
        [&](const Case &, const gentest::runner::MeasurementCaseFailure &, std::string_view) { ++failures; });

    expect(status.ok && successes == 1 && failures == 0, "the benchmark completes");
    expect(result.calibration_iters == 1 + 2 + 4 + 8, "calibration tries 1, 2, 4 and 8 calls");
    expect(result.iters_per_epoch == 8, "the measured epoch uses the last calibrated count (8), not 16");
    expect(result.total_iters == 8, "one measured epoch of 8 calls");
    expect(g_bench_calls == 15 + 8, "no call count beyond the cap is ever executed");
    if (g_failures != 0)
        std::cerr << "iters_per_epoch=" << result.iters_per_epoch << " calibration_iters=" << result.calibration_iters
                  << " calls=" << g_bench_calls << '\n';
    return g_failures == 0 ? 0 : 1;
}

// PowerShell treats U+2018, U+2019, U+201A and U+201B as single-quote delimiters too.
int check_replay_quoting() {
    using gentest::runner::quote_replay_argument;
    using gentest::runner::ReplayShell;
    const std::string_view left  = "\xE2\x80\x98"; // U+2018
    const std::string_view right = "\xE2\x80\x99"; // U+2019
    const std::string_view low   = "\xE2\x80\x9A"; // U+201A
    const std::string_view high  = "\xE2\x80\x9B"; // U+201B

    const auto power = [](std::string_view value) { return quote_replay_argument(value, ReplayShell::PowerShell); };
    const auto posix = [](std::string_view value) { return quote_replay_argument(value, ReplayShell::Posix); };
    const auto twice = [](std::string_view quote) { return std::string(quote) + std::string(quote); };

    expect(power("plain") == "'plain'", "PowerShell: plain text is wrapped");
    expect(power("it's") == "'it''s'", "PowerShell: ASCII apostrophe is doubled");
    for (const auto quote : {left, right, low, high}) {
        const std::string value = "a" + std::string(quote) + "b";
        expect(power(value) == "'a" + twice(quote) + "b'", "PowerShell: typographic single quote is doubled");
        expect(posix(value) == "'" + value + "'", "POSIX: typographic single quotes are ordinary characters");
    }
    // Neighbouring code points and truncated sequences are left alone.
    expect(power("\xE2\x80\x9C") == "'\xE2\x80\x9C'", "PowerShell: U+201C is not a single-quote delimiter");
    expect(power("\xE2\x80\x97") == "'\xE2\x80\x97'", "PowerShell: U+2017 is not a single-quote delimiter");
    expect(power("\xE2\x80") == "'\xE2\x80'", "PowerShell: truncated sequence is copied");
    expect(posix("it's") == "'it'\\''s'", "POSIX: apostrophe closes, escapes and reopens");

    const std::string name = "--run=suite/" + std::string(right) + "case";
    const std::string command =
        gentest::runner::replay_command("C:\\t dir\\it" + std::string(left) + ".exe", {name, "--kind=test"}, ReplayShell::PowerShell);
    expect(command.find("-FilePath 'C:\\t dir\\it" + twice(left) + ".exe'") != std::string::npos,
           "PowerShell command doubles quotes in the executable path");
    expect(command.find(R"(-ArgumentList '"--run=suite/)" + twice(right) + R"(case" "--kind=test"')") != std::string::npos,
           "PowerShell command doubles quotes in the argument list");
    expect(gentest::runner::replay_command("./t", {"--run=a b"}, ReplayShell::Posix) == "'./t' '--run=a b'", "POSIX command shape");
    if (g_failures != 0)
        std::cerr << command << '\n';
    return g_failures == 0 ? 0 : 1;
}

// Failure paths are shortened at whole path components, never inside one.
int check_failure_path_trim() {
    using gentest::detail::trim_source_path;
    expect(trim_source_path("/home/u/contests/2024/a.cpp") == "/home/u/contests/2024/a.cpp", "'contests/' is not a 'tests/' component");
    expect(trim_source_path("/home/u/mytools/a.cpp") == "/home/u/mytools/a.cpp", "'mytools/' is not a 'tools/' component");
    expect(trim_source_path("/home/u/proj/tests/unit/a.cpp") == "tests/unit/a.cpp", "repo-relative tests path");
    expect(trim_source_path("tests/unit/a.cpp") == "tests/unit/a.cpp", "already relative path");
    expect(trim_source_path("/x/contests/tests/a.cpp") == "tests/a.cpp", "a later real component still matches");
    expect(trim_source_path("/x/mysrc/include/gentest/a.h") == "include/gentest/a.h", "'mysrc/' is skipped, 'include/' matches");
    expect(trim_source_path("/x/latests/src/a.cpp") == "src/a.cpp", "'latests/' is skipped, 'src/' matches");
    expect(trim_source_path("/x/src/proj/tests/a.cpp") == "tests/a.cpp", "'tests/' keeps priority over 'src/'");
    expect(trim_source_path("/x/proj/tools/src/a.cpp") == "src/a.cpp", "'src/' keeps priority over 'tools/'");
    expect(trim_source_path("/x/proj/./tests/../tests/a.cpp") == "tests/a.cpp", "paths are normalized first");
    expect(trim_source_path("/opt/other/lib/a.cpp") == "/opt/other/lib/a.cpp", "paths without a marker are unchanged");
    return g_failures == 0 ? 0 : 1;
}

// A hand-written death-tagged benchmark reports body entry through the death-test
// marker without leaving the runner: the marker names the case, and an unwritable
// marker is a case failure.
int check_measured_death_marker(const std::filesystem::path &directory) {
    static constexpr std::string_view kName = "regressions/measured_death_marker/bench";
    static constexpr std::array       kTags{std::string_view{"death"}};
    Case                              bench = make_case(kName);
    bench.is_benchmark                      = true;
    bench.tags                              = kTags;
    const std::array cases{bench};

    std::error_code ec;
    std::filesystem::remove_all(directory, ec);
    std::filesystem::create_directories(directory);

    const auto run = [&](const std::filesystem::path &marker) {
        const std::string         marker_argument = "--gentest-death-marker=" + marker.string();
        std::vector<const char *> arguments{"runner_selection_reports",
                                            "--run=regressions/measured_death_marker/bench",
                                            "--kind=bench",
                                            "--include-death",
                                            "--no-color",
                                            "--bench-epochs=1",
                                            "--bench-warmup=0",
                                            "--bench-min-epoch-time-s=0",
                                            "--bench-max-total-time-s=0.01",
                                            marker_argument.c_str()};
        return gentest::run_cases(cases, arguments);
    };

    const auto marker = directory / "marker.txt";
    expect(run(marker) == 0, "the benchmark passes with a writable marker");
    const std::string contents = read_file(marker);
    expect(contents.starts_with(std::string(kName) + "\ncompleted"), "the marker names the case and records completion");

    expect(run(directory / "missing" / "marker.txt") == 1, "an unwritable marker fails the case instead of terminating");
    if (g_failures != 0)
        std::cerr << "marker contents: '" << contents << "'\n";
    return g_failures == 0 ? 0 : 1;
}

bool contains(std::string_view haystack, std::string_view needle) { return haystack.find(needle) != std::string_view::npos; }

std::filesystem::path fresh_directory(const std::filesystem::path &directory) {
    std::error_code ec;
    std::filesystem::remove_all(directory, ec);
    std::filesystem::create_directories(directory);
    return directory;
}

void write_file(const std::filesystem::path &path, std::string_view contents) {
    std::ofstream out(path, std::ios::binary);
    out << contents;
}

gentest::runner::ReportItem make_item(std::string name, gentest::runner::Outcome outcome, double time_s) {
    gentest::runner::ReportItem item;
    item.suite   = "regressions";
    item.name    = std::move(name);
    item.outcome = outcome;
    item.time_s  = time_s;
    return item;
}

gentest::runner::ReportItem make_blocked_item() {
    auto item        = make_item("reports/blocked", gentest::runner::Outcome::Blocked, 1.2e-05);
    item.skipped     = true;
    item.skip_reason = "blocked: fixture down";
    return item;
}

// Decimal comma and digit grouping, as a user-installed global locale could impose.
struct GroupingPunct : std::numpunct<char> {
    char        do_decimal_point() const override { return ','; }
    char        do_thousands_sep() const override { return '.'; }
    std::string do_grouping() const override { return "\3"; }
};

// JUnit: a blocked case is an <error>, and numbers are locale-independent fixed-point decimals.
int check_junit_report(const std::filesystem::path &directory) {
    const auto junit = fresh_directory(directory) / "report.xml";

    gentest::runner::RunAccumulator acc;
    acc.report_items.push_back(make_blocked_item());
    acc.report_items.push_back(make_item("reports/slow", gentest::runner::Outcome::Pass, 1234.5));
    auto skipped        = make_item("reports/skipped", gentest::runner::Outcome::Skip, 0.0);
    skipped.skipped     = true;
    skipped.skip_reason = "not today";
    acc.report_items.push_back(skipped);
    for (int i = 0; i < 1200; ++i)
        acc.report_items.push_back(make_item("reports/many/" + std::to_string(i), gentest::runner::Outcome::Pass, 0.5));

    const std::locale previous = std::locale::global(std::locale(std::locale::classic(), new GroupingPunct));
    const std::string path     = junit.string();
    const bool        ok       = gentest::runner::write_reports(acc, {.junit_path = path.c_str(), .allure_dir = nullptr});
    std::locale::global(previous);

    const std::string xml = read_file(junit);
    expect(ok, "the JUnit report is written");
    expect(contains(xml, R"(tests="1203" failures="0" skipped="1" errors="1">)"), "blocked counts as an error; counts are ungrouped");
    expect(contains(xml, R"(<error type="blocked" message="blocked: fixture down"/>)"), "blocked case carries an <error> element");
    expect(contains(xml, R"(<skipped message="not today"/>)"), "an ordinary skip stays <skipped>");
    expect(!contains(xml, R"(<skipped message="blocked)") && !contains(xml, "<skipped/>"), "blocked case is not written as <skipped>");
    expect(contains(xml, R"(name="reports/blocked" time="0.000012000")"), "small times are fixed-point, not exponent notation");
    expect(contains(xml, R"(name="reports/slow" time="1234.500000000")"), "times ignore the global locale");
    if (g_failures != 0)
        std::cerr << xml.substr(0, 600) << '\n';
    return g_failures == 0 ? 0 : 1;
}

bool is_valid_utf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto  lead   = static_cast<unsigned char>(text[i]);
        std::size_t length = 0;
        if (lead < 0x80U)
            length = 1;
        else if (lead >= 0xC2U && lead <= 0xDFU)
            length = 2;
        else if (lead >= 0xE0U && lead <= 0xEFU)
            length = 3;
        else if (lead >= 0xF0U && lead <= 0xF4U)
            length = 4;
        if (length == 0 || i + length > text.size())
            return false;
        for (std::size_t k = 1; k < length; ++k) {
            if ((static_cast<unsigned char>(text[i + k]) & 0xC0U) != 0x80U)
                return false;
        }
        i += length;
    }
    return true;
}

// Allure: blocked is "broken", every string is valid UTF-8, and result files left by an
// earlier run are removed while files gentest does not name are kept.
int check_allure_report(const std::filesystem::path &directory) {
    const auto       results = fresh_directory(directory);
    const std::array stale{"result-7-result.json", "result-7-attachment.txt", "result-12-custom-1.bin", "runtime-record-3-0.cbor",
                           "runtime-case-9-index.json"};
    const std::array foreign{
        "categories.json",       "environment.properties",  "result-x-result.json",    "results-1-result.json", "result-7",
        "runtime-record-3.cbor", "runtime-case-index.json", "my-result-1-result.json", "runtime-record-3-0.txt"};
    for (const auto *name : stale)
        write_file(results / name, R"({"name":"stale","status":"passed"})");
    for (const auto *name : foreign)
        write_file(results / name, "keep");
    std::filesystem::create_directories(results / "result-8-directory");

    const std::string               bad = "bad\xFF\xC3(bytes";
    gentest::runner::RunAccumulator acc;
    acc.report_items.push_back(make_blocked_item());
    auto failed  = make_item("reports/name-" + bad, gentest::runner::Outcome::Fail, 0.25);
    failed.suite = "suite-" + bad;
    failed.failures.push_back("failure-" + bad);
    failed.attachments.push_back(gentest::runner::ReportAttachment{
        .name           = "attachment-" + bad,
        .mime_type      = "text/" + bad,
        .file_extension = ".txt",
        .contents       = std::make_shared<const std::string>("payload"),
    });
    acc.report_items.push_back(failed);
    auto skipped        = make_item("reports/skipped", gentest::runner::Outcome::Skip, 0.0);
    skipped.skipped     = true;
    skipped.skip_reason = "xfail: reason-" + bad;
    acc.report_items.push_back(skipped);
    auto blocked_bad        = make_blocked_item();
    blocked_bad.skip_reason = "blocked: reason-" + bad;
    acc.report_items.push_back(blocked_bad);
    acc.infra_errors.push_back("infra-" + bad);

    const std::string path = results.string();
    expect(gentest::runner::write_reports(acc, {.junit_path = nullptr, .allure_dir = path.c_str()}), "the Allure results are written");

    for (const auto *name : stale)
        expect(!std::filesystem::exists(results / name), std::string("stale gentest file is removed: ") + name);
    for (const auto *name : foreign)
        expect(read_file(results / name) == "keep", std::string("foreign file is untouched: ") + name);
    expect(std::filesystem::is_directory(results / "result-8-directory"), "directories are never removed");

    for (int index = 0; index < 5; ++index) {
        const auto        file = results / ("result-" + std::to_string(index) + "-result.json");
        const std::string json = read_file(file);
        expect(!json.empty(), "result file exists: " + file.filename().string());
        expect(is_valid_utf8(json), "result file is valid UTF-8: " + file.filename().string());
        expect(!contains(json, "stale"), "result file belongs to this run: " + file.filename().string());
    }
    const std::string blocked = read_file(results / "result-0-result.json");
    expect(contains(blocked, R"("status":"broken")"), "a blocked case has the failing status 'broken'");
    expect(contains(blocked, R"({"name":"blocked","value":"true"})"), "a blocked case keeps its blocked label");
    expect(contains(blocked, R"("message":"blocked: fixture down")"), "a blocked case keeps its reason");
    expect(contains(read_file(results / "result-1-result.json"), "failure-bad\xEF\xBF\xBD\xEF\xBF\xBD(bytes"),
           "malformed bytes become U+FFFD");
    expect(contains(read_file(results / "result-2-result.json"), R"("status":"skipped")"), "an ordinary skip stays skipped");
    return g_failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char **argv) {
    const std::string_view mode = argc > 1 ? argv[1] : "";
    if (mode == "wildcard-literal-star")
        return check_wildcard_literal_star();
    if (mode == "calibration-cap")
        return check_calibration_cap();
    if (mode == "replay-quoting")
        return check_replay_quoting();
    if (mode == "failure-path-trim")
        return check_failure_path_trim();
    if (mode == "measured-death-marker" && argc == 3)
        return check_measured_death_marker(argv[2]);
    if (mode == "junit-report" && argc == 3)
        return check_junit_report(argv[2]);
    if (mode == "allure-report" && argc == 3)
        return check_allure_report(argv[2]);
    std::cerr << "usage: runner_selection_reports <mode> [args]\n";
    return 2;
}
