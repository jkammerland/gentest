#include "runner_test_executor.h"

#include "runner_case_invoker.h"
#include "runner_fixture_runtime.h"
#include "runner_result_model.h"
#include "runner_test_plan.h"

#include <algorithm>
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
using ReportItem = gentest::runner::ReportItem;

void record_failure_summary(TestRunContext &state, std::string_view name, std::vector<std::string> issues) {
    if (!state.acc)
        return;
    gentest::runner::record_failure_summary(*state.acc, name, std::move(issues));
}

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
    ++c.executed;
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
            ++c.failed;
            ++c.failures;
            record_failure_summary(state, test.name, std::vector<std::string>{issue});
            if (state.acc)
                gentest::runner::add_error_annotation(*state.acc, test.file, test.line, test.name, issue);
        }
        const long long dur_ms = static_cast<long long>(rr.time_s * 1000.0 + 0.5);
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
            rr.outcome             = Outcome::XFail;
            rr.skipped             = true;
            rr.skip_reason         = rr.xfail_reason.empty() ? "xfail" : std::string("xfail: ") + rr.xfail_reason;
            const long long dur_ms = static_cast<long long>(rr.time_s * 1000.0 + 0.5);
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
        const long long dur_ms = static_cast<long long>(rr.time_s * 1000.0 + 0.5);
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
        record_failure_summary(state, test.name, std::vector<std::string>{std::move(xpass_issue)});
        if (state.acc)
            gentest::runner::add_error_annotation(*state.acc, test.file, test.line, test.name, rr.failures.front());
        return rr;
    }

    rr.failures = ctxinfo->failures;

    if (!ctxinfo->failures.empty()) {
        rr.outcome = Outcome::Fail;
        ++c.failed;
        ++c.failures;
        const long long dur_ms = static_cast<long long>(rr.time_s * 1000.0 + 0.5);
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
        record_failure_summary(state, test.name, std::move(failure_lines));
    } else if (!threw_non_skip) {
        const long long dur_ms = static_cast<long long>(rr.time_s * 1000.0 + 0.5);
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
        const long long dur_ms = static_cast<long long>(rr.time_s * 1000.0 + 0.5);
        if (state.color_output) {
            fmt::print(stderr, fmt::fg(fmt::color::red), "[ FAIL ]");
            fmt::print(stderr, " {} ({} ms)\n", test.name, dur_ms);
        } else {
            fmt::print(stderr, "[ FAIL ] {} ({} ms)\n", test.name, dur_ms);
        }
        fmt::print(stderr, "\n");
        record_failure_summary(state, test.name, std::vector<std::string>{"fatal assertion or exception (no message)"});
    }
    return rr;
}

void execute_and_record(TestRunContext &state, const gentest::Case &test, void *ctx, TestCounters &c) {
    RunResult rr = execute_one(state, test, ctx, c);
    if (!state.record_results || !state.acc)
        return;
    ReportItem item;
    item.suite       = std::string(test.suite);
    item.name        = std::string(test.name);
    item.time_s      = rr.time_s;
    item.skipped     = rr.skipped;
    item.skip_reason = rr.skip_reason.empty() ? std::string(test.skip_reason) : rr.skip_reason;
    item.outcome     = rr.outcome;
    item.failures    = std::move(rr.failures);
    item.logs        = std::move(rr.logs);
    item.timeline    = std::move(rr.timeline);
    for (auto sv : test.tags)
        item.tags.emplace_back(sv);
    for (auto sv : test.requirements)
        item.requirements.emplace_back(sv);
    state.acc->report_items.push_back(std::move(item));
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
        record_failure_summary(state, test.name, std::vector<std::string>{issue});
        if (state.acc)
            gentest::runner::add_error_annotation(*state.acc, test.file, test.line, test.name, issue);
    }
    if (!state.record_results || !state.acc)
        return;

    ReportItem item;
    item.suite       = std::string(test.suite);
    item.name        = std::string(test.name);
    item.time_s      = 0.0;
    item.skipped     = true;
    item.outcome     = Outcome::Skip;
    item.skip_reason = std::move(reason);
    if (infra_failure)
        item.failures.push_back(issue);
    for (auto sv : test.tags)
        item.tags.emplace_back(sv);
    for (auto sv : test.requirements)
        item.requirements.emplace_back(sv);
    state.acc->report_items.push_back(std::move(item));
}

} // namespace

bool run_tests_once(TestRunContext &state, std::span<const gentest::Case> cases, std::span<const std::size_t> idxs, bool shuffle,
                    std::uint64_t base_seed, bool fail_fast, TestCounters &counters) {
    const auto plans = gentest::runner::build_suite_execution_plan(cases, idxs, shuffle, base_seed);

    for (const auto &plan : plans) {
        for (auto i : plan.free_like) {
            execute_and_record(state, cases[i], nullptr, counters);
            if (fail_fast && counters.failures > 0)
                return true;
        }

        const auto run_groups = [&](const std::vector<gentest::runner::FixtureGroupPlan> &groups) -> bool {
            for (const auto &group : groups) {
                for (auto i : group.idxs) {
                    const auto &t   = cases[i];
                    void       *ctx = nullptr;
                    std::string reason;
                    if (!gentest::runner::acquire_case_fixture(t, ctx, reason)) {
                        const std::string msg = reason.empty() ? std::string("fixture allocation returned null") : reason;
                        record_synthetic_skip(state, t, msg, counters, true);
                        if (fail_fast && counters.failures > 0)
                            return true;
                        continue;
                    }
                    execute_and_record(state, t, ctx, counters);
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
