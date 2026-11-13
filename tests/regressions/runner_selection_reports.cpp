// Regression checks for runner selection, CLI validation and report writers.
// Each mode is registered as its own CTest entry so one failure stays isolated.
#include "../../src/runner_cli.h"
#include "../../src/runner_measured_executor.h"
#include "../../src/runner_replay.h"
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
    std::cerr << "usage: runner_selection_reports <mode> [args]\n";
    return 2;
}
