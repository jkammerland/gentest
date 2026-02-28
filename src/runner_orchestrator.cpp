#include "runner_orchestrator.h"

#include "runner_fixture_runtime.h"
#include "runner_measured_executor.h"
#include "runner_reporting.h"
#include "runner_selector.h"
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

using Outcome = gentest::runner::Outcome;
using ReportItem = gentest::runner::ReportItem;
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

bool iequals(std::string_view lhs, std::string_view rhs) {
    if (lhs.size() != rhs.size())
        return false;
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        const char a = lhs[i];
        const char b = rhs[i];
        if (a == b)
            continue;
        const char al = (a >= 'A' && a <= 'Z') ? static_cast<char>(a - 'A' + 'a') : a;
        const char bl = (b >= 'A' && b <= 'Z') ? static_cast<char>(b - 'A' + 'a') : b;
        if (al != bl)
            return false;
    }
    return true;
}

bool has_tag_ci(const gentest::Case &test, std::string_view tag) {
    for (auto t : test.tags) {
        if (iequals(t, tag))
            return true;
    }
    return false;
}

std::string join_span(std::span<const std::string_view> items, char sep) {
    std::string out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i != 0)
            out.push_back(sep);
        out.append(items[i]);
    }
    return out;
}

void record_failure_summary(OrchestratorState &state, std::string_view name, std::vector<std::string> issues) {
    gentest::runner::record_failure_summary(state.acc, name, std::move(issues));
}

void record_runner_level_failure(OrchestratorState &state, std::string_view name, std::string message) {
    gentest::runner::record_runner_level_failure(state.acc, name, std::move(message));
}

void record_measured_failure_report_item(OrchestratorState &state, const gentest::Case &c, const MeasurementCaseFailure &failure,
                                         std::string_view failure_message) {
    if (!state.record_results)
        return;

    ReportItem item;
    item.suite  = std::string(c.suite);
    item.name   = std::string(c.name);
    item.time_s = 0.0;

    if (failure.skipped) {
        item.skipped     = true;
        item.outcome     = Outcome::Skip;
        item.skip_reason = failure.reason;
        if (failure.infra_failure) {
            const std::string issue = item.skip_reason.empty() ? std::string("shared fixture unavailable") : item.skip_reason;
            item.failures.push_back(issue);
        }
    } else if (!failure_message.empty()) {
        item.failures.emplace_back(failure_message);
    } else if (!failure.reason.empty()) {
        item.failures.push_back(failure.reason);
    }

    for (auto sv : c.tags)
        item.tags.emplace_back(sv);
    for (auto sv : c.requirements)
        item.requirements.emplace_back(sv);

    state.acc.report_items.push_back(std::move(item));
}

void record_measured_failure_summary(OrchestratorState &state, const gentest::Case &c, const MeasurementCaseFailure &failure,
                                     std::string_view failure_message) {
    if (failure.skipped && !failure.infra_failure)
        return;

    std::string issue;
    if (!failure_message.empty()) {
        issue = std::string(failure_message);
    } else if (!failure.reason.empty()) {
        issue = failure.reason;
    } else if (failure.skipped) {
        issue = "measured case skipped";
    } else {
        issue = "measured case failed";
    }

    record_failure_summary(state, c.name, std::vector<std::string>{issue});
    ++state.acc.measured_failures;
}

template <typename Result> void record_measured_success_report_item(OrchestratorState &state, const gentest::Case &c, const Result &result) {
    if (!state.record_results)
        return;

    ReportItem item;
    item.suite   = std::string(c.suite);
    item.name    = std::string(c.name);
    item.time_s  = result.wall_time_s;
    item.outcome = Outcome::Pass;

    for (auto sv : c.tags)
        item.tags.emplace_back(sv);
    for (auto sv : c.requirements)
        item.requirements.emplace_back(sv);

    state.acc.report_items.push_back(std::move(item));
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

        if (opt.shuffle && !has_selection)
            fmt::print("Shuffle seed: {}\n", opt.shuffle_seed);
        for (std::size_t iter = 0; iter < opt.repeat_n; ++iter) {
            if (opt.shuffle && has_selection)
                fmt::print("Shuffle seed: {}\n", opt.shuffle_seed);
            tests_stopped = gentest::runner::run_tests_once(
                test_state, kCases, std::span<const std::size_t>{test_idxs.data(), test_idxs.size()}, opt.shuffle, opt.shuffle_seed,
                opt.fail_fast, counters);
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
                record_measured_success_report_item(state, measured, result);
            },
            [&](const gentest::Case &measured, const MeasurementCaseFailure &failure, std::string_view failure_message) {
                record_measured_failure_summary(state, measured, failure, failure_message);
                record_measured_failure_report_item(state, measured, failure, failure_message);
            });
    }
    if (!fixture_runtime_blocked && !(opt.fail_fast && (tests_stopped || bench_status.stopped))) {
        jitter_status = gentest::runner::run_selected_jitters(
            kCases, std::span<const std::size_t>{jitter_idxs.data(), jitter_idxs.size()}, opt, opt.fail_fast,
            [&](const gentest::Case &measured, const gentest::runner::JitterResult &result) {
                record_measured_success_report_item(state, measured, result);
            },
            [&](const gentest::Case &measured, const MeasurementCaseFailure &failure, std::string_view failure_message) {
                record_measured_failure_summary(state, measured, failure, failure_message);
                record_measured_failure_report_item(state, measured, failure, failure_message);
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
            should_write = !state.acc.report_items.empty();
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

    if (!test_idxs.empty() || !state.acc.failure_items.empty()) {
        const std::size_t failed_count = counters.failed + state.acc.measured_failures + state.acc.infra_errors.size();
        std::string       summary;
        summary.reserve(128 + state.acc.failure_items.size() * 64);
        fmt::format_to(std::back_inserter(summary), "Summary: passed {}/{}; failed {}; skipped {}; xfail {}; xpass {}.\n", counters.passed,
                       counters.total, failed_count, counters.skipped, counters.xfail, counters.xpass);
        if (!state.acc.failure_items.empty()) {
            std::map<std::string, std::vector<std::string>> grouped;
            for (const auto &item : state.acc.failure_items) {
                auto &issues = grouped[item.name];
                for (const auto &issue : item.issues) {
                    if (std::find(issues.begin(), issues.end(), issue) == issues.end()) {
                        issues.push_back(issue);
                    }
                }
            }
            summary.append("Failed tests:\n");
            for (const auto &[name, issues] : grouped) {
                fmt::format_to(std::back_inserter(summary), "  {}:\n", name);
                for (const auto &issue : issues) {
                    fmt::format_to(std::back_inserter(summary), "    {}\n", issue);
                }
            }
        }
        fmt::print("{}", summary);
    }

    const bool ok = (counters.failures == 0) && bench_status.ok && jitter_status.ok && fixture_guard.ok();
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
            std::string sections;
            if (!test.tags.empty() || !test.requirements.empty() || test.should_skip) {
                sections.push_back(' ');
                sections.push_back('[');
                bool first = true;
                if (!test.tags.empty()) {
                    sections.append("tags=");
                    sections.append(join_span(test.tags, ','));
                    first = false;
                }
                if (!test.requirements.empty()) {
                    if (!first)
                        sections.push_back(';');
                    sections.append("requires=");
                    sections.append(join_span(test.requirements, ','));
                    first = false;
                }
                if (test.should_skip) {
                    if (!first)
                        sections.push_back(';');
                    sections.append("skip");
                    if (!test.skip_reason.empty()) {
                        sections.push_back('=');
                        sections.append(test.skip_reason);
                    }
                }
                sections.push_back(']');
            }
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
    case SelectionStatus::CaseNotFound:
        fmt::print(stderr, "Case not found: {}\n", opt.run_exact);
        return kExitCaseNotFound;
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
        fmt::print(stderr, "jitter filter matched 0 benchmarks: {}\n", opt.filter_pat);
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
    case SelectionStatus::DeathExcludedAll:
        fmt::print("Executed 0 case(s). (death tests excluded; use --include-death)\n");
        return 0;
    }

    if (selection.filtered_death > 0) {
        fmt::print("Note: excluded {} death test(s). Use --include-death to run them.\n", selection.filtered_death);
    }

    return run_execution(kCases, opt, selection, has_selection);
}

} // namespace gentest::runner
