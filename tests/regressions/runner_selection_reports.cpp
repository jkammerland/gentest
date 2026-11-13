// Regression checks for runner selection, CLI validation and report writers.
// Each mode is registered as its own CTest entry so one failure stays isolated.
#include "../../src/runner_cli.h"
#include "../../src/runner_measured_executor.h"
#include "../../src/runner_selector.h"
#include "gentest/detail/generated_runtime.h"
#include "gentest/runner.h"

#include <array>
#include <cstddef>
#include <iostream>
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
    const std::array cases{bench};
    const std::array<std::size_t, 1> indices{0};

    CliOptions options;
    options.bench_cfg.min_epoch_time_s           = 1.0e9; // never satisfied
    options.bench_cfg.min_total_time_s           = 0.0;
    options.bench_cfg.warmup_epochs              = 0;
    options.bench_cfg.measure_epochs             = 1;
    options.bench_cfg.max_calibration_iterations = 8;

    std::size_t successes = 0;
    std::size_t failures  = 0;
    gentest::runner::BenchResult result;
    const auto status = gentest::runner::run_selected_benches(
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

} // namespace

int main(int argc, char **argv) {
    const std::string_view mode = argc > 1 ? argv[1] : "";
    if (mode == "wildcard-literal-star")
        return check_wildcard_literal_star();
    if (mode == "calibration-cap")
        return check_calibration_cap();
    std::cerr << "usage: runner_selection_reports <mode> [args]\n";
    return 2;
}
