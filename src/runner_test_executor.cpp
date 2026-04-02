#include "runner_test_executor.h"

#include "runner_case_invoker.h"
#include "runner_fixture_runtime.h"
#include "runner_result_model.h"
#include "runner_test_plan.h"

#include <algorithm>
#include <cmath>
#include <fmt/color.h>
#include <fmt/format.h>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gentest::runner {
namespace {

using Outcome   = gentest::runner::Outcome;
using RunResult = gentest::runner::RunResult;

long long duration_ms(double seconds) { return std::llround(seconds * 1000.0); }

RunResult execute_one(TestRunContext &state, const gentest::Case &test, void *ctx, TestCounters &c) {
    RunResult rr;
    if (test.should_skip) {
        ++c.total;
        ++c.skipped;
        rr.skipped             = true;
        rr.outcome             = Outcome::Skip;
        rr.skip_reason         = std::string(test.skip_reason);
        const long long dur_ms = 0LL;
        if (state.color_output) {
            fmt::print(fmt::fg(fmt::color::yellow), "[ SKIP ]");
            if (!test.skip_reason.empty())
                fmt::print(" {} :: {} ({} ms)\n", test.name, test.skip_reason, dur_ms);
            else
                fmt::print(" {} ({} ms)\n", test.name, dur_ms);
        } else {
            if (!test.skip_reason.empty())
                fmt::print("[ SKIP ] {} :: {} ({} ms)\n", test.name, test.skip_reason, dur_ms);
            else
                fmt::print("[ SKIP ] {} ({} ms)\n", test.name, dur_ms);
        }
        return rr;
    }
    ++c.total;
    auto       inv             = gentest::runner::invoke_case_once(test, ctx, gentest::detail::BenchPhase::None,
                                                                   gentest::runner::UnhandledExceptionPolicy::RecordAsFailure);
    auto       ctxinfo         = inv.ctxinfo;
    const bool runtime_skipped = (inv.exception == gentest::runner::InvokeException::Skip);
    const bool threw_non_skip =
        (inv.exception != gentest::runner::InvokeException::None && inv.exception != gentest::runner::InvokeException::Skip);
    rr.time_s   = inv.elapsed_s;
    rr.logs     = ctxinfo->logs;
    rr.timeline = ctxinfo->event_lines;

    bool        should_skip = false;
    std::string runtime_skip_reason;
    auto        runtime_skip_kind = gentest::detail::TestContextInfo::RuntimeSkipKind::User;
    bool        is_xfail          = false;
    std::string xfail_reason;
    {
        std::lock_guard<std::mutex> lk(ctxinfo->mtx);
        should_skip         = runtime_skipped && ctxinfo->runtime_skip_requested.load(std::memory_order_relaxed);
        runtime_skip_reason = ctxinfo->runtime_skip_reason;
        runtime_skip_kind   = ctxinfo->runtime_skip_kind;
        is_xfail            = ctxinfo->xfail_requested;
        xfail_reason        = ctxinfo->xfail_reason;
    }

    const bool has_failures = !ctxinfo->failures.empty();

    if (should_skip && !has_failures && !threw_non_skip) {
        ++c.skipped;
        rr.skipped     = true;
        rr.outcome     = Outcome::Skip;
        rr.skip_reason = std::move(runtime_skip_reason);
        if (runtime_skip_kind == gentest::detail::TestContextInfo::RuntimeSkipKind::SharedFixtureInfra) {
            const std::string issue = rr.skip_reason.empty() ? std::string("shared fixture unavailable") : rr.skip_reason;
            rr.failures.push_back(issue);
            rr.summary_issues.push_back(issue);
            ++c.failed;
            ++c.failures;
            if (state.acc)
                gentest::runner::add_error_annotation(*state.acc, test.file, test.line, test.name, issue);
        }
        const auto dur_ms = duration_ms(rr.time_s);
        if (state.color_output) {
            fmt::print(fmt::fg(fmt::color::yellow), "[ SKIP ]");
            if (!rr.skip_reason.empty())
                fmt::print(" {} :: {} ({} ms)\n", test.name, rr.skip_reason, dur_ms);
            else
                fmt::print(" {} ({} ms)\n", test.name, dur_ms);
        } else {
            if (!rr.skip_reason.empty())
                fmt::print("[ SKIP ] {} :: {} ({} ms)\n", test.name, rr.skip_reason, dur_ms);
            else
                fmt::print("[ SKIP ] {} ({} ms)\n", test.name, dur_ms);
        }
        return rr;
    }

    if (is_xfail && !should_skip) {
        rr.xfail_reason = std::move(xfail_reason);
        if (has_failures || threw_non_skip) {
            ++c.xfail;
            ++c.skipped;
            rr.outcome        = Outcome::XFail;
            rr.skipped        = true;
            rr.skip_reason    = rr.xfail_reason.empty() ? "xfail" : std::string("xfail: ") + rr.xfail_reason;
            const auto dur_ms = duration_ms(rr.time_s);
            if (state.color_output) {
                fmt::print(fmt::fg(fmt::color::cyan), "[ XFAIL ]");
                if (!rr.xfail_reason.empty())
                    fmt::print(" {} :: {} ({} ms)\n", test.name, rr.xfail_reason, dur_ms);
                else
                    fmt::print(" {} ({} ms)\n", test.name, dur_ms);
            } else {
                if (!rr.xfail_reason.empty())
                    fmt::print("[ XFAIL ] {} :: {} ({} ms)\n", test.name, rr.xfail_reason, dur_ms);
                else
                    fmt::print("[ XFAIL ] {} ({} ms)\n", test.name, dur_ms);
            }
            return rr;
        }
        rr.outcome = Outcome::XPass;
        rr.failures.push_back(rr.xfail_reason.empty() ? "xpass" : std::string("xpass: ") + rr.xfail_reason);
        ++c.xpass;
        ++c.failed;
        ++c.failures;
        const auto dur_ms = duration_ms(rr.time_s);
        if (state.color_output) {
            fmt::print(stderr, fmt::fg(fmt::color::red), "[ XPASS ]");
            if (!rr.xfail_reason.empty())
                fmt::print(stderr, " {} :: {} ({} ms)\n", test.name, rr.xfail_reason, dur_ms);
            else
                fmt::print(stderr, " {} ({} ms)\n", test.name, dur_ms);
        } else {
            if (!rr.xfail_reason.empty())
                fmt::print(stderr, "[ XPASS ] {} :: {} ({} ms)\n", test.name, rr.xfail_reason, dur_ms);
            else
                fmt::print(stderr, "[ XPASS ] {} ({} ms)\n", test.name, dur_ms);
        }
        fmt::print(stderr, "{}\n\n", rr.failures.front());
        std::string xpass_issue = rr.xfail_reason.empty() ? "XPASS" : std::string("XPASS: ") + rr.xfail_reason;
        rr.summary_issues.push_back(std::move(xpass_issue));
        if (state.acc)
            gentest::runner::add_error_annotation(*state.acc, test.file, test.line, test.name, rr.failures.front());
        return rr;
    }

    rr.failures = ctxinfo->failures;

    if (!ctxinfo->failures.empty()) {
        rr.outcome = Outcome::Fail;
        ++c.failed;
        ++c.failures;
        const auto dur_ms = duration_ms(rr.time_s);
        if (state.color_output) {
            fmt::print(stderr, fmt::fg(fmt::color::red), "[ FAIL ]");
            fmt::print(stderr, " {} :: {} issue(s) ({} ms)\n", test.name, ctxinfo->failures.size(), dur_ms);
        } else {
            fmt::print(stderr, "[ FAIL ] {} :: {} issue(s) ({} ms)\n", test.name, ctxinfo->failures.size(), dur_ms);
        }
        std::size_t              failure_printed = 0;
        std::vector<std::string> failure_lines;
        for (std::size_t i = 0; i < ctxinfo->event_lines.size(); ++i) {
            const char  kind = (i < ctxinfo->event_kinds.size() ? ctxinfo->event_kinds[i] : 'L');
            const auto &ln   = ctxinfo->event_lines[i];
            if (kind == 'F') {
                fmt::print(stderr, "{}\n", ln);
                failure_lines.push_back(ln);
                std::string_view file    = test.file;
                unsigned         line_no = test.line;
                if (failure_printed < ctxinfo->failure_locations.size()) {
                    const auto &fl = ctxinfo->failure_locations[failure_printed];
                    if (!fl.file.empty() && fl.line > 0) {
                        file    = fl.file;
                        line_no = fl.line;
                    }
                }
                if (state.acc)
                    gentest::runner::add_error_annotation(*state.acc, file, line_no, test.name, ln);
                ++failure_printed;
            } else {
                fmt::print(stderr, "{}\n", ln);
            }
        }
        fmt::print(stderr, "\n");
        if (failure_lines.empty() && !ctxinfo->failures.empty())
            failure_lines.push_back(ctxinfo->failures.front());
        rr.summary_issues = std::move(failure_lines);
    } else if (!threw_non_skip) {
        const auto dur_ms = duration_ms(rr.time_s);
        if (state.color_output) {
            fmt::print(fmt::fg(fmt::color::green), "[ PASS ]");
            fmt::print(" {} ({} ms)\n", test.name, dur_ms);
        } else {
            fmt::print("[ PASS ] {} ({} ms)\n", test.name, dur_ms);
        }
        rr.outcome = Outcome::Pass;
        ++c.passed;
    } else {
        rr.outcome = Outcome::Fail;
        ++c.failed;
        ++c.failures;
        std::string fallback_issue = inv.message.empty() ? "fatal assertion or exception (no message)" : inv.message;
        rr.failures.push_back(fallback_issue);
        const auto dur_ms = duration_ms(rr.time_s);
        if (state.color_output) {
            fmt::print(stderr, fmt::fg(fmt::color::red), "[ FAIL ]");
            fmt::print(stderr, " {} ({} ms)\n", test.name, dur_ms);
        } else {
            fmt::print(stderr, "[ FAIL ] {} ({} ms)\n", test.name, dur_ms);
        }
        fmt::print(stderr, "\n");
        rr.summary_issues.push_back(fallback_issue);
        if (state.acc)
            gentest::runner::add_error_annotation(*state.acc, test.file, test.line, test.name, fallback_issue);
    }
    return rr;
}

void execute_and_record(TestRunContext &state, const gentest::Case &test, void *ctx, TestCounters &c) {
    RunResult rr = execute_one(state, test, ctx, c);
    if (!state.acc)
        return;
    gentest::runner::record_case_result(*state.acc, test, std::move(rr), state.record_results);
}

void record_synthetic_skip(TestRunContext &state, const gentest::Case &test, std::string reason, TestCounters &c,
                           bool infra_failure = false) {
    ++c.total;
    ++c.skipped;
    const long long dur_ms = 0LL;
    if (state.color_output) {
        fmt::print(fmt::fg(fmt::color::yellow), "[ SKIP ]");
        if (!reason.empty()) {
            fmt::print(" {} :: {} ({} ms)\n", test.name, reason, dur_ms);
        } else {
            fmt::print(" {} ({} ms)\n", test.name, dur_ms);
        }
    } else {
        if (!reason.empty()) {
            fmt::print("[ SKIP ] {} :: {} ({} ms)\n", test.name, reason, dur_ms);
        } else {
            fmt::print("[ SKIP ] {} ({} ms)\n", test.name, dur_ms);
        }
    }

    const std::string issue = reason.empty() ? std::string("fixture allocation returned null") : reason;
    if (infra_failure) {
        ++c.failed;
        ++c.failures;
        if (state.acc)
            gentest::runner::add_error_annotation(*state.acc, test.file, test.line, test.name, issue);
    }
    if (!state.acc)
        return;

    RunResult rr;
    rr.skipped     = true;
    rr.outcome     = Outcome::Skip;
    rr.skip_reason = std::move(reason);
    if (infra_failure) {
        rr.failures.push_back(issue);
        rr.summary_issues.push_back(issue);
    }
    gentest::runner::record_case_result(*state.acc, test, std::move(rr), state.record_results);
}

} // namespace

bool run_tests_once(TestRunContext &state, std::span<const gentest::Case> cases, std::span<const SuiteExecutionPlan> plans, bool fail_fast,
                    TestCounters &counters) {
    for (const auto &plan : plans) {
        for (auto i : plan.free_like) {
            execute_and_record(state, cases[i], nullptr, counters);
            if (fail_fast && counters.failures > 0)
                return true;
        }

        const auto run_groups = [&](const std::vector<gentest::runner::FixtureGroupPlan> &groups) -> bool {
            for (const auto &group : groups) {
                void       *group_ctx = nullptr;
                std::string group_reason;
                if (!group.idxs.empty() && !gentest::runner::acquire_case_fixture(cases[group.idxs.front()], group_ctx, group_reason)) {
                    const std::string msg =
                        group_reason.empty() ? std::string("fixture allocation returned null") : std::move(group_reason);
                    for (auto i : group.idxs) {
                        record_synthetic_skip(state, cases[i], msg, counters, true);
                        if (fail_fast && counters.failures > 0)
                            return true;
                    }
                    continue;
                }

                for (auto i : group.idxs) {
                    execute_and_record(state, cases[i], group_ctx, counters);
                    if (fail_fast && counters.failures > 0)
                        return true;
                }
            }
            return false;
        };

        if (run_groups(plan.suite_groups))
            return true;
        if (run_groups(plan.global_groups))
            return true;
    }

    return false;
}

} // namespace gentest::runner
