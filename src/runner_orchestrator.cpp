#include "runner_orchestrator.h"

#include "runner_fixture_runtime.h"
#include "runner_measured_executor.h"
#include "runner_measured_report.h"
#include "runner_reporting.h"
#include "runner_selector.h"
#include "runner_tag_utils.h"
#include "runner_test_executor.h"

#include <algorithm>
#include <fmt/format.h>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gentest::runner {
namespace {

using Outcome         = gentest::runner::Outcome;
using RunResult       = gentest::runner::RunResult;
using SelectionStatus = gentest::runner::SelectionStatus;

struct SharedFixtureRunGuard {
    gentest::runner::detail::SharedFixtureRuntimeSession session{};
    bool                                                 setup_ok    = true;
    bool                                                 teardown_ok = true;
    bool                                                 finalized   = false;
    std::vector<std::string>                             setup_errors;
    std::vector<std::string>                             teardown_errors;

    SharedFixtureRunGuard() { setup_ok = gentest::runner::detail::setup_shared_fixture_runtime(setup_errors, session); }

    void finalize() {
        if (!finalized) {
            teardown_ok = gentest::runner::detail::teardown_shared_fixture_runtime(teardown_errors, session);
            finalized   = true;
        }
    }

    bool ok() const { return setup_ok && teardown_ok; }
    bool gate_rejected() const { return session.gate_rejected; }

    ~SharedFixtureRunGuard() { finalize(); }
};

struct OrchestratorState {
    bool                            color_output   = true;
    bool                            record_results = false;
    gentest::runner::RunAccumulator acc;
};

std::string join_span(std::span<const std::string_view> items, char sep) {
    std::size_t total_size = items.empty() ? 0 : (items.size() - 1);
    for (std::string_view item : items) {
        total_size += item.size();
    }

    fmt::memory_buffer out;
    out.reserve(total_size);
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i != 0) {
            out.push_back(sep);
        }
        fmt::format_to(std::back_inserter(out), "{}", items[i]);
    }
    return fmt::to_string(out);
}

std::string format_list_sections(const gentest::Case &test) {
    if (test.tags.empty() && test.requirements.empty() && !test.should_skip) {
        return {};
    }

    fmt::memory_buffer sections;
    fmt::format_to(std::back_inserter(sections), " [gentest:");
    bool first = true;

    const auto append_separator = [&] {
        if (!first) {
            sections.push_back(';');
        }
        first = false;
    };

    if (!test.tags.empty()) {
        append_separator();
        fmt::format_to(std::back_inserter(sections), "tags={}", join_span(test.tags, ','));
    }
    if (!test.requirements.empty()) {
        append_separator();
        fmt::format_to(std::back_inserter(sections), "requires={}", join_span(test.requirements, ','));
    }
    if (test.should_skip) {
        append_separator();
        if (test.skip_reason.empty()) {
            fmt::format_to(std::back_inserter(sections), "skip");
        } else {
            fmt::format_to(std::back_inserter(sections), "skip={}", test.skip_reason);
        }
    }

    sections.push_back(']');
    return fmt::to_string(sections);
}

void record_runner_level_failure(OrchestratorState &state, std::string_view name, std::string message) {
    gentest::runner::record_runner_level_failure(state.acc, name, std::move(message));
}

RunResult make_measured_failure_result(const MeasurementCaseFailure &failure, std::string_view failure_message) {
    RunResult result;
    if (failure.skipped) {
        if (failure.infra_failure) {
            result.skipped          = true;
            result.outcome          = Outcome::Blocked;
            result.time_s           = failure.time_s;
            const std::string issue = failure.reason.empty() ? std::string("shared fixture unavailable") : failure.reason;
            result.skip_reason      = fmt::format("blocked: {}", issue);
            return result;
        }
        result.skipped     = true;
        result.outcome     = Outcome::Skip;
        result.time_s      = failure.time_s;
        result.skip_reason = failure.reason;
        return result;
    }

    result.outcome = Outcome::Fail;
    result.time_s  = failure.time_s;
    std::string issue;
    if (!failure_message.empty()) {
        issue = std::string(failure_message);
    } else if (!failure.reason.empty()) {
        issue = failure.reason;
    } else {
        issue = "measured case failed";
    }
    result.failures.push_back(issue);
    result.summary_issues.push_back(issue);
    return result;
}

RunResult make_measured_success_result(double wall_time_s, std::vector<ReportAttachment> attachments = {}) {
    RunResult run_result;
    run_result.time_s      = wall_time_s;
    run_result.outcome     = Outcome::Pass;
    run_result.attachments = std::move(attachments);
    return run_result;
}

void record_measured_result(OrchestratorState &state, const gentest::Case &c, RunResult result) {
    if (!state.record_results && result.summary_issues.empty()) {
        return;
    }
    gentest::runner::record_case_result(state.acc, c, std::move(result), state.record_results);
}

int run_execution(std::span<const gentest::Case> kCases, const CliOptions &opt, const SelectionResult &selection, bool has_selection) {
    const auto &test_idxs   = selection.test_idxs;
    const auto &bench_idxs  = selection.bench_idxs;
    const auto &jitter_idxs = selection.jitter_idxs;

    OrchestratorState state{};
    state.color_output   = opt.color_output;
    state.record_results = (opt.junit_path != nullptr) || (opt.allure_dir != nullptr);

    SharedFixtureRunGuard fixture_guard;
    TestCounters          counters;

    if (!fixture_guard.setup_ok) {
        for (const auto &message : fixture_guard.setup_errors) {
            record_runner_level_failure(state, "gentest/shared_fixture_setup", message);
        }
    }
    const bool fixture_runtime_blocked = fixture_guard.gate_rejected();

    bool tests_stopped = false;
    if (!fixture_runtime_blocked && !test_idxs.empty()) {
        TestRunContext test_state{};
        test_state.color_output   = state.color_output;
        test_state.record_results = state.record_results;
        test_state.acc            = &state.acc;
        const auto test_plans     = gentest::runner::build_suite_execution_plan(
            kCases, std::span<const std::size_t>{test_idxs.data(), test_idxs.size()}, opt.shuffle, opt.shuffle_seed);

        if (opt.shuffle && !has_selection)
            fmt::print("Shuffle seed: {}\n", opt.shuffle_seed);
        for (std::size_t iter = 0; iter < opt.repeat_n; ++iter) {
            if (opt.shuffle && has_selection)
                fmt::print("Shuffle seed: {}\n", opt.shuffle_seed);
            tests_stopped = gentest::runner::run_tests_once(test_state, kCases, test_plans, opt.fail_fast, counters);
            if (tests_stopped)
                break;
        }
    }

    TimedRunStatus bench_status{};
    TimedRunStatus jitter_status{};
    if (!fixture_runtime_blocked && !(opt.fail_fast && tests_stopped)) {
        bench_status = gentest::runner::run_selected_benches(
            kCases, std::span<const std::size_t>{bench_idxs.data(), bench_idxs.size()}, opt, opt.fail_fast,
            [&](const gentest::Case &measured, const gentest::runner::BenchResult &result) {
                std::vector<ReportAttachment> attachments;
                if (opt.allure_dir != nullptr) {
                    attachments = make_bench_allure_attachments(measured, result);
                }
                record_measured_result(state, measured, make_measured_success_result(result.wall_time_s, std::move(attachments)));
            },
            [&](const gentest::Case &measured, const MeasurementCaseFailure &failure, std::string_view failure_message) {
                record_measured_result(state, measured, make_measured_failure_result(failure, failure_message));
            });
    }
    if (!fixture_runtime_blocked && !(opt.fail_fast && (tests_stopped || bench_status.stopped))) {
        jitter_status = gentest::runner::run_selected_jitters(
            kCases, std::span<const std::size_t>{jitter_idxs.data(), jitter_idxs.size()}, opt, opt.fail_fast,
            [&](const gentest::Case &measured, const gentest::runner::JitterResult &result) {
                std::vector<ReportAttachment> attachments;
                if (opt.allure_dir != nullptr) {
                    attachments = make_jitter_allure_attachments(measured, result, opt.jitter_bins);
                }
                record_measured_result(state, measured, make_measured_success_result(result.wall_time_s, std::move(attachments)));
            },
            [&](const gentest::Case &measured, const MeasurementCaseFailure &failure, std::string_view failure_message) {
                record_measured_result(state, measured, make_measured_failure_result(failure, failure_message));
            });
    }

    fixture_guard.finalize();
    if (!fixture_guard.teardown_ok) {
        if (fixture_guard.teardown_errors.empty()) {
            record_runner_level_failure(state, "gentest/shared_fixture_teardown", "shared fixture teardown failed");
        } else {
            for (const auto &message : fixture_guard.teardown_errors) {
                record_runner_level_failure(state, "gentest/shared_fixture_teardown", message);
            }
        }
    }

    if (state.record_results) {
        const bool ran_any_case = !selection.idxs.empty();
        bool       should_write = false;
        if (opt.junit_path != nullptr) {
            should_write = ran_any_case || !state.acc.infra_errors.empty();
        } else if (opt.allure_dir != nullptr) {
            should_write = !state.acc.report_items.empty() || !state.acc.infra_errors.empty();
        }
        if (should_write) {
            gentest::runner::write_reports(state.acc, gentest::runner::ReportConfig{
                                                          .junit_path = opt.junit_path,
                                                          .allure_dir = opt.allure_dir,
                                                      });
        }
    }

    if (opt.github_annotations) {
        gentest::runner::emit_github_annotations(state.acc);
    }

    if (!selection.idxs.empty() || !state.acc.failure_items.empty()) {
        const std::size_t  passed_count  = counters.passed + bench_status.passed + jitter_status.passed;
        const std::size_t  total_count   = counters.total + bench_status.total + jitter_status.total;
        const std::size_t  failed_count  = counters.failed + bench_status.failed + jitter_status.failed + state.acc.infra_errors.size();
        const std::size_t  skipped_count = counters.skipped + bench_status.skipped + jitter_status.skipped;
        const std::size_t  blocked_count = counters.blocked + bench_status.blocked + jitter_status.blocked;
        fmt::memory_buffer summary;
        fmt::format_to(std::back_inserter(summary), "Summary: passed {}/{}; failed {}; skipped {}; blocked {}; xfail {}; xpass {}.\n",
                       passed_count, total_count, failed_count, skipped_count, blocked_count, counters.xfail, counters.xpass);
        if (!state.acc.failure_items.empty()) {
            fmt::format_to(std::back_inserter(summary), "Failed tests:\n");
            for (const auto &item : state.acc.failure_items) {
                if (!item.file.empty() && item.line != 0) {
                    fmt::format_to(std::back_inserter(summary), "  {} ({}:{}):\n", item.name, item.file, item.line);
                } else {
                    fmt::format_to(std::back_inserter(summary), "  {}:\n", item.name);
                }
                std::vector<std::string> unique_issues;
                unique_issues.reserve(item.issues.size());
                for (const auto &issue : item.issues) {
                    if (std::ranges::find(unique_issues, issue) == unique_issues.end()) {
                        unique_issues.push_back(issue);
                    }
                }
                for (const auto &issue : unique_issues) {
                    fmt::format_to(std::back_inserter(summary), "    {}\n", issue);
                }
            }
        }
        fmt::print("{}", std::string_view(summary.data(), summary.size()));
    }

    const bool ok = (counters.failures == 0) && (counters.blocked == 0) && bench_status.ok && jitter_status.ok && fixture_guard.ok() &&
                    state.acc.infra_errors.empty();
    return ok ? 0 : 1;
}

} // namespace

int run_from_options(std::span<const gentest::Case> kCases, const CliOptions &opt) {
    constexpr int kExitCaseNotFound = 3;

    switch (opt.mode) {
    case Mode::Help:
#ifdef GENTEST_VERSION_STR
        fmt::print("gentest v{}\n", GENTEST_VERSION_STR);
#else
        fmt::print("gentest v{}\n", "0.0.0");
#endif
        fmt::print("Usage: [options]\n");
        fmt::print("  --help                Show this help\n");
        fmt::print("  --list-tests          List test names (one per line)\n");
        fmt::print("  --list                List tests with metadata\n");
        fmt::print("  --list-death          List death test names (one per line)\n");
        fmt::print("  --list-benches        List benchmark/jitter names (one per line)\n");
        fmt::print("  --run=<name>          Run a single case by exact name\n");
        fmt::print("  --filter=<pattern>    Run cases matching wildcard pattern (*, ?)\n");
        fmt::print("  --kind=<kind>         Restrict to kind: all|test|bench|jitter (default all)\n");
        fmt::print("  --include-death       Allow running tests tagged 'death'\n");
        fmt::print("  --no-color            Disable colorized output (or set NO_COLOR/GENTEST_NO_COLOR)\n");
        fmt::print("  --github-annotations  Emit GitHub Actions annotations (::error ...) on failures\n");
        fmt::print("  --junit=<file>        Write JUnit XML report to file\n");
        fmt::print("  --allure-dir=<dir>    Write Allure result JSON files into directory\n");
        fmt::print("  --time-unit=<mode>    Time display unit: auto|ns (default auto)\n");
        fmt::print("  --fail-fast           Stop after the first failing case\n");
        fmt::print("  --repeat=N            Repeat selected tests N times (default 1)\n");
        fmt::print("  --shuffle             Shuffle tests (respects fixture/grouping)\n");
        fmt::print("  --seed N              RNG seed used with --shuffle\n");
        fmt::print("\nBenchmark options:\n");
        fmt::print("  --bench-table         Print a summary table per suite (runs benches)\n");
        fmt::print("  --bench-min-epoch-time-s=<sec>  Minimum epoch time\n");
        fmt::print("  --bench-epochs=<N>    Measurement epochs (default 12)\n");
        fmt::print("  --bench-warmup=<N>    Warmup epochs (default 1)\n");
        fmt::print("  --bench-min-total-time-s=<sec>  Min total time per benchmark (may exceed --bench-epochs)\n");
        fmt::print("  --bench-max-total-time-s=<sec>  Max total time per benchmark\n");
        fmt::print("\nJitter options:\n");
        fmt::print("  --jitter-bins=<N>     Histogram bins (default 10)\n");
        return 0;
    case Mode::ListTests:
        for (const auto &t : kCases)
            fmt::print("{}\n", t.name);
        return 0;
    case Mode::ListMeta:
        for (const auto &test : kCases) {
            const std::string sections = format_list_sections(test);
            fmt::print("{}{} ({}:{})\n", test.name, sections, test.file, test.line);
        }
        return 0;
    case Mode::ListDeath:
        for (const auto &test : kCases) {
            if (has_tag_ci(test, "death") && !test.should_skip) {
                fmt::print("{}\n", test.name);
            }
        }
        return 0;
    case Mode::ListBenches:
        for (const auto &t : kCases)
            if (t.is_benchmark || t.is_jitter)
                fmt::print("{}\n", t.name);
        return 0;
    case Mode::Execute: break;
    }

    const auto selection     = gentest::runner::select_cases(kCases, opt);
    const bool has_selection = selection.has_selection;

    switch (selection.status) {
    case SelectionStatus::Ok: break;
    case SelectionStatus::CaseNotFound: fmt::print(stderr, "Case not found: {}\n", opt.run_exact); return kExitCaseNotFound;
    case SelectionStatus::KindMismatch:
        fmt::print(stderr, "Case '{}' does not match --kind={}\n", opt.run_exact, gentest::runner::kind_to_string(opt.kind));
        return 1;
    case SelectionStatus::Ambiguous:
        fmt::print(stderr, "Case name is ambiguous: {}\n", opt.run_exact);
        fmt::print(stderr, "Matches:\n");
        for (auto idx : selection.ambiguous_matches)
            fmt::print(stderr, "  {}\n", kCases[idx].name);
        return 1;
    case SelectionStatus::FilterNoBenchMatch:
        fmt::print(stderr, "benchmark filter matched 0 benchmarks: {}\n", opt.filter_pat);
        fmt::print(stderr, "hint: use --list-benches to see available names\n");
        return 1;
    case SelectionStatus::FilterNoJitterMatch:
        fmt::print(stderr, "jitter filter matched 0 jitter benchmarks: {}\n", opt.filter_pat);
        fmt::print(stderr, "hint: use --list-benches to see available names\n");
        return 1;
    case SelectionStatus::ZeroSelected:
        switch (opt.kind) {
        case KindFilter::Test: fmt::print("Executed 0 test(s).\n"); break;
        case KindFilter::Bench: fmt::print("Executed 0 benchmark(s).\n"); break;
        case KindFilter::Jitter: fmt::print("Executed 0 jitter benchmark(s).\n"); break;
        case KindFilter::All: fmt::print("Executed 0 case(s).\n"); break;
        }
        return 0;
    case SelectionStatus::DeathExcludedExact:
        fmt::print(stderr, "Case '{}' is tagged as a death test; rerun with --include-death\n", opt.run_exact);
        return 1;
    case SelectionStatus::DeathExcludedAll: fmt::print("Executed 0 case(s). (death tests excluded; use --include-death)\n"); return 0;
    }

    if (selection.filtered_death > 0) {
        fmt::print("Note: excluded {} death test(s). Use --include-death to run them.\n", selection.filtered_death);
    }

    return run_execution(kCases, opt, selection, has_selection);
}

} // namespace gentest::runner
