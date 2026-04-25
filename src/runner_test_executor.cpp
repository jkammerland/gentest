#include "runner_test_executor.h"

#include "gentest/detail/runtime_context.h"
#include "runner_case_invoker.h"
#include "runner_context_scope.h"
#include "runner_fixture_runtime.h"
#include "runner_result_model.h"
#include "runner_test_plan.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <exception>
#include <fmt/color.h>
#include <fmt/format.h>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gentest::runner {
namespace {

using Outcome   = gentest::runner::Outcome;
using RunResult = gentest::runner::RunResult;

long long duration_ms(double seconds) { return std::llround(seconds * 1000.0); }

auto collect_pass_visible_timeline(const std::shared_ptr<gentest::detail::TestContextInfo> &ctxinfo) -> std::vector<std::string> {
    std::vector<std::string> lines;
    lines.reserve(ctxinfo->event_lines.size());
    for (std::size_t i = 0; i < ctxinfo->event_lines.size(); ++i) {
        const char kind = (i < ctxinfo->event_kinds.size() ? ctxinfo->event_kinds[i] : 'L');
        if (kind == 'A')
            lines.push_back(ctxinfo->event_lines[i]);
    }
    return lines;
}

RunResult make_static_skip_result(TestRunContext &state, const gentest::Case &test, TestCounters &c) {
    RunResult rr;
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

RunResult finish_invoke_result(TestRunContext &state, const gentest::Case &test, const InvokeResult &inv, TestCounters &c) {
    RunResult  rr;
    auto       ctxinfo         = inv.ctxinfo;
    const bool runtime_skipped = (inv.exception == gentest::runner::InvokeException::Skip);
    const bool runtime_blocked = (inv.exception == gentest::runner::InvokeException::Blocked);
    const bool threw_non_skip =
        (inv.exception != gentest::runner::InvokeException::None && inv.exception != gentest::runner::InvokeException::Skip &&
         inv.exception != gentest::runner::InvokeException::Blocked);
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

    if (runtime_blocked) {
        ++c.blocked;
        ++c.failures;
        rr.skipped     = true;
        rr.outcome     = Outcome::Blocked;
        rr.skip_reason = inv.message.empty() ? "async test cannot resume" : inv.message;
        rr.summary_issues.push_back(fmt::format("BLOCKED: {}", rr.skip_reason));
        const auto dur_ms = duration_ms(rr.time_s);
        if (state.color_output) {
            fmt::print(fmt::fg(fmt::color::yellow), "[ BLOCKED ]");
            fmt::print(" {} :: {} ({} ms)\n", test.name, rr.skip_reason, dur_ms);
        } else {
            fmt::print("[ BLOCKED ] {} :: {} ({} ms)\n", test.name, rr.skip_reason, dur_ms);
        }
        if (state.acc)
            gentest::runner::add_error_annotation(*state.acc, test.file, test.line, test.name, rr.summary_issues.front());
        return rr;
    }

    if (should_skip && !has_failures && !threw_non_skip) {
        rr.skip_reason = std::move(runtime_skip_reason);
        if (runtime_skip_kind == gentest::detail::TestContextInfo::RuntimeSkipKind::SharedFixtureInfra) {
            const std::string issue = rr.skip_reason.empty() ? std::string("shared fixture unavailable") : rr.skip_reason;
            rr.skipped              = true;
            rr.outcome              = Outcome::Blocked;
            rr.skip_reason          = fmt::format("blocked: {}", issue);
            ++c.blocked;
            const auto dur_ms = duration_ms(rr.time_s);
            if (state.color_output) {
                fmt::print(fmt::fg(fmt::color::yellow), "[ BLOCKED ]");
                fmt::print(" {} :: {} ({} ms)\n", test.name, issue, dur_ms);
            } else {
                fmt::print("[ BLOCKED ] {} :: {} ({} ms)\n", test.name, issue, dur_ms);
            }
            return rr;
        }

        ++c.skipped;
        rr.skipped        = true;
        rr.outcome        = Outcome::Skip;
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
            rr.skip_reason    = rr.xfail_reason.empty() ? "xfail" : fmt::format("xfail: {}", rr.xfail_reason);
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
        rr.failures.push_back(rr.xfail_reason.empty() ? "xpass" : fmt::format("xpass: {}", rr.xfail_reason));
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
        std::string xpass_issue = rr.xfail_reason.empty() ? "XPASS" : fmt::format("XPASS: {}", rr.xfail_reason);
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
        rr.timeline = collect_pass_visible_timeline(ctxinfo);
        for (const auto &ln : rr.timeline) {
            fmt::print("{}\n", ln);
        }
        if (!rr.timeline.empty())
            fmt::print("\n");
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

RunResult execute_one(TestRunContext &state, const gentest::Case &test, void *ctx, TestCounters &c) {
    if (test.should_skip) {
        return make_static_skip_result(state, test, c);
    }
    ++c.total;
    auto inv = gentest::runner::invoke_case_once(test, ctx, gentest::detail::BenchPhase::None,
                                                 gentest::runner::UnhandledExceptionPolicy::RecordAsFailure);
    return finish_invoke_result(state, test, inv, c);
}

struct AsyncCaseRun {
    std::size_t                                       case_index  = 0;
    void                                             *fixture_ctx = nullptr;
    std::shared_ptr<gentest::detail::TestContextInfo> ctxinfo;
    gentest::detail::AsyncTaskPtr                     task;
    std::chrono::steady_clock::time_point             start;
    std::chrono::steady_clock::time_point             end;
    InvokeException                                   exception = InvokeException::None;
    std::string                                       message;
};

class BatchAsyncScheduler final : public gentest::detail::AsyncScheduler {
  public:
    explicit BatchAsyncScheduler(std::vector<AsyncCaseRun> &runs) : runs_(runs) {}

    void post(std::coroutine_handle<> handle) override {
        if (!handle || handle.done()) {
            return;
        }
        blocked_handles_.erase(handle.address());
        ready_.push_back(handle);
    }

    void block(std::coroutine_handle<> handle, std::string reason) override {
        if (!handle) {
            return;
        }
        const auto owner = owner_for(handle);
        blocked_handles_[handle.address()] =
            BlockedHandle{.owner = owner, .reason = reason.empty() ? std::string("async test cannot resume") : std::move(reason)};
    }

    void attach_child(std::coroutine_handle<> child, std::coroutine_handle<> parent) override {
        if (!child || !parent) {
            return;
        }
        const auto parent_owner = owner_for(parent);
        if (parent_owner < runs_.size()) {
            owners_[child.address()] = parent_owner;
        }
    }

    void add_top_level(std::size_t run_index, gentest::detail::AsyncTask &task) {
        task.set_scheduler(this);
        owners_[task.handle().address()] = run_index;
        post(task.handle());
    }

    void run() {
        gentest::detail::AsyncSchedulerScope scheduler_scope(this);
        while (!ready_.empty()) {
            auto handle = ready_.front();
            ready_.pop_front();
            if (!handle || handle.done()) {
                continue;
            }
            const auto owner = owner_for(handle);
            if (owner >= runs_.size()) {
                continue;
            }
            auto                                             &run = runs_[owner];
            gentest::runner::detail::CurrentTestAdoptionScope current_scope(run.ctxinfo);
            try {
                handle.resume();
            } catch (const std::exception &e) {
                run.exception = InvokeException::StdException;
                run.message   = fmt::format("std::exception: {}", e.what());
            } catch (...) {
                run.exception = InvokeException::Unknown;
                run.message   = "unknown exception";
            }
        }

        for (std::size_t i = 0; i < runs_.size(); ++i) {
            auto &run = runs_[i];
            if (!run.task || !run.task->handle() || run.task->handle().done() || run.exception != InvokeException::None) {
                continue;
            }
            run.exception = InvokeException::Blocked;
            run.message   = blocked_reason_for(i);
        }
    }

  private:
    struct BlockedHandle {
        std::size_t owner = 0;
        std::string reason;
    };

    [[nodiscard]] auto owner_for(std::coroutine_handle<> handle) const -> std::size_t {
        const auto it = owners_.find(handle.address());
        if (it == owners_.end()) {
            return runs_.size();
        }
        return it->second;
    }

    [[nodiscard]] auto blocked_reason_for(std::size_t owner) const -> std::string {
        for (const auto &entry : blocked_handles_) {
            const auto &blocked = entry.second;
            if (blocked.owner == owner && !blocked.reason.empty()) {
                return blocked.reason;
            }
        }
        return "async test cannot resume";
    }

    std::vector<AsyncCaseRun>                &runs_;
    std::deque<std::coroutine_handle<>>       ready_;
    std::unordered_map<void *, std::size_t>   owners_;
    std::unordered_map<void *, BlockedHandle> blocked_handles_;
};

void classify_async_exception(AsyncCaseRun &run) {
    if (run.exception != InvokeException::None || !run.task) {
        return;
    }
    const auto ex = run.task->exception();
    if (!ex) {
        return;
    }

    gentest::runner::detail::CurrentTestAdoptionScope current_scope(run.ctxinfo);
    try {
        std::rethrow_exception(ex);
    } catch (const gentest::detail::blocked_exception &e) {
        run.exception = InvokeException::Blocked;
        run.message   = e.reason();
    } catch (const gentest::detail::skip_exception &) { run.exception = InvokeException::Skip; } catch (const gentest::assertion &e) {
        run.exception = InvokeException::Assertion;
        run.message   = e.message();
    } catch (const gentest::failure &e) {
        run.exception = InvokeException::Failure;
        gentest::detail::record_failure(fmt::format("FAIL() :: {}", e.what()));
        run.message = e.what();
    } catch (const std::exception &e) {
        run.exception = InvokeException::StdException;
        gentest::detail::record_failure(fmt::format("unexpected std::exception: {}", e.what()));
        run.message = fmt::format("std::exception: {}", e.what());
    } catch (...) {
        run.exception = InvokeException::Unknown;
        gentest::detail::record_failure("unknown exception");
        run.message = "unknown exception";
    }
}

auto finish_async_run(AsyncCaseRun &run) -> InvokeResult {
    classify_async_exception(run);
    gentest::runner::detail::finish_active_test_context(run.ctxinfo);
    gentest::detail::flush_current_buffer_for(run.ctxinfo.get());
    run.end = std::chrono::steady_clock::now();
    InvokeResult inv;
    inv.ctxinfo   = run.ctxinfo;
    inv.exception = run.exception;
    inv.message   = std::move(run.message);
    inv.elapsed_s = std::chrono::duration<double>(run.end - run.start).count();
    return inv;
}

void schedule_async_case(std::vector<AsyncCaseRun> &runs, const gentest::Case &test, std::size_t case_index, void *fixture_ctx) {
    AsyncCaseRun run;
    run.case_index  = case_index;
    run.fixture_ctx = fixture_ctx;
    run.ctxinfo     = gentest::runner::detail::make_active_test_context(test.name);
    run.start       = std::chrono::steady_clock::now();

    {
        gentest::runner::detail::CurrentTestAdoptionScope current_scope(run.ctxinfo);
        try {
            if (test.async_fn) {
                run.task = test.async_fn(fixture_ctx);
            }
            if (!run.task) {
                run.exception = InvokeException::Failure;
                run.message   = "async test did not create a coroutine task";
                gentest::detail::record_failure(run.message);
            }
        } catch (const gentest::detail::skip_exception &) { run.exception = InvokeException::Skip; } catch (const gentest::assertion &e) {
            run.exception = InvokeException::Assertion;
            run.message   = e.message();
        } catch (const gentest::failure &e) {
            run.exception = InvokeException::Failure;
            gentest::detail::record_failure(fmt::format("FAIL() :: {}", e.what()));
            run.message = e.what();
        } catch (const std::exception &e) {
            run.exception = InvokeException::StdException;
            gentest::detail::record_failure(fmt::format("unexpected std::exception: {}", e.what()));
            run.message = fmt::format("std::exception: {}", e.what());
        } catch (...) {
            run.exception = InvokeException::Unknown;
            gentest::detail::record_failure("unknown exception");
            run.message = "unknown exception";
        }
    }

    runs.push_back(std::move(run));
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
    const std::string issue  = reason.empty() ? std::string("fixture allocation returned null") : reason;
    const long long   dur_ms = 0LL;
    if (infra_failure) {
        ++c.blocked;
        if (state.color_output) {
            fmt::print(fmt::fg(fmt::color::yellow), "[ BLOCKED ]");
            fmt::print(" {} :: {} ({} ms)\n", test.name, issue, dur_ms);
        } else {
            fmt::print("[ BLOCKED ] {} :: {} ({} ms)\n", test.name, issue, dur_ms);
        }
    } else {
        ++c.skipped;
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
    }
    if (!state.acc)
        return;

    RunResult rr;
    rr.skipped     = true;
    rr.outcome     = infra_failure ? Outcome::Blocked : Outcome::Skip;
    rr.skip_reason = infra_failure ? fmt::format("blocked: {}", issue) : std::move(reason);
    gentest::runner::record_case_result(*state.acc, test, std::move(rr), state.record_results);
}

bool plans_include_async_cases(std::span<const gentest::Case> cases, std::span<const SuiteExecutionPlan> plans) {
    for (const auto &plan : plans) {
        for (auto i : plan.free_like) {
            if (cases[i].async_fn) {
                return true;
            }
        }
        for (const auto &group : plan.suite_groups) {
            for (auto i : group.idxs) {
                if (cases[i].async_fn) {
                    return true;
                }
            }
        }
        for (const auto &group : plan.global_groups) {
            for (auto i : group.idxs) {
                if (cases[i].async_fn) {
                    return true;
                }
            }
        }
    }
    return false;
}

void finish_and_record_async_runs(TestRunContext &state, std::span<const gentest::Case> cases, std::vector<AsyncCaseRun> &runs,
                                  TestCounters &counters) {
    BatchAsyncScheduler scheduler(runs);
    for (std::size_t run_index = 0; run_index < runs.size(); ++run_index) {
        auto &run = runs[run_index];
        if (run.task && run.exception == InvokeException::None) {
            scheduler.add_top_level(run_index, *run.task);
        }
    }
    scheduler.run();

    for (auto &run : runs) {
        ++counters.total;
        auto      inv = finish_async_run(run);
        RunResult rr  = finish_invoke_result(state, cases[run.case_index], inv, counters);
        if (state.acc) {
            gentest::runner::record_case_result(*state.acc, cases[run.case_index], std::move(rr), state.record_results);
        }
    }
}

bool run_tests_async_batch(TestRunContext &state, std::span<const gentest::Case> cases, std::span<const SuiteExecutionPlan> plans,
                           bool fail_fast, TestCounters &counters) {
    std::vector<AsyncCaseRun> async_runs;

    const auto handle_case = [&](std::size_t i, void *ctx) {
        const auto &test = cases[i];
        if (test.should_skip) {
            RunResult rr = make_static_skip_result(state, test, counters);
            if (state.acc) {
                gentest::runner::record_case_result(*state.acc, test, std::move(rr), state.record_results);
            }
            return;
        }
        if (test.async_fn) {
            schedule_async_case(async_runs, test, i, ctx);
            return;
        }
        execute_and_record(state, test, ctx, counters);
    };

    for (const auto &plan : plans) {
        for (auto i : plan.free_like) {
            handle_case(i, nullptr);
        }

        const auto collect_groups = [&](const std::vector<gentest::runner::FixtureGroupPlan> &groups) {
            for (const auto &group : groups) {
                void       *group_ctx = nullptr;
                std::string group_reason;
                if (!group.idxs.empty() && !gentest::runner::acquire_case_fixture(cases[group.idxs.front()], group_ctx, group_reason)) {
                    const std::string msg =
                        group_reason.empty() ? std::string("fixture allocation returned null") : std::move(group_reason);
                    for (auto i : group.idxs) {
                        record_synthetic_skip(state, cases[i], msg, counters, true);
                    }
                    continue;
                }

                for (auto i : group.idxs) {
                    handle_case(i, group_ctx);
                }
            }
        };

        collect_groups(plan.suite_groups);
        collect_groups(plan.global_groups);
    }

    finish_and_record_async_runs(state, cases, async_runs, counters);
    return fail_fast && counters.failures > 0;
}

} // namespace

bool run_tests_once(TestRunContext &state, std::span<const gentest::Case> cases, std::span<const SuiteExecutionPlan> plans, bool fail_fast,
                    TestCounters &counters) {
    if (plans_include_async_cases(cases, plans)) {
        return run_tests_async_batch(state, cases, plans, fail_fast, counters);
    }

    for (const auto &plan : plans) {
        for (auto i : plan.free_like) {
            execute_and_record(state, cases[i], nullptr, counters);
            if (fail_fast && (counters.failures > 0 || counters.blocked > 0))
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
                        if (fail_fast && (counters.failures > 0 || counters.blocked > 0))
                            return true;
                    }
                    continue;
                }

                for (auto i : group.idxs) {
                    execute_and_record(state, cases[i], group_ctx, counters);
                    if (fail_fast && (counters.failures > 0 || counters.blocked > 0))
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
