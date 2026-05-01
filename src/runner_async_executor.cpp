#include "runner_async_executor.h"

#include "gentest/detail/runtime_context.h"
#include "runner_async_scheduler.h"
#include "runner_async_state.h"
#include "runner_async_status_renderer.h"
#include "runner_case_result.h"
#include "runner_context_scope.h"
#include "runner_fixture_runtime.h"
#include "runner_reporting.h"

#include <atomic>
#include <chrono>
#include <fmt/color.h>
#include <fmt/format.h>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gentest::runner {
namespace {

auto outcome_color(Outcome outcome) -> fmt::color {
    switch (outcome) {
    case Outcome::Pass: return fmt::color::green;
    case Outcome::Fail:
    case Outcome::XPass: return fmt::color::red;
    case Outcome::Skip:
    case Outcome::Blocked: return fmt::color::yellow;
    case Outcome::XFail: return fmt::color::cyan;
    }
    return fmt::color::white;
}

auto async_live_status_for(const RunResult &result) -> AsyncLiveStatus {
    switch (result.outcome) {
    case Outcome::Pass: return AsyncLiveStatus::Pass;
    case Outcome::Fail: return AsyncLiveStatus::Fail;
    case Outcome::Skip: return AsyncLiveStatus::Skip;
    case Outcome::Blocked: return AsyncLiveStatus::Blocked;
    case Outcome::XFail: return AsyncLiveStatus::XFail;
    case Outcome::XPass: return AsyncLiveStatus::XPass;
    }
    return AsyncLiveStatus::Fail;
}

auto async_live_detail_for(const RunResult &result) -> std::string {
    switch (result.outcome) {
    case Outcome::Fail:
        if (!result.failures.empty()) {
            return fmt::format("{} issue(s)", result.failures.size());
        }
        if (!result.summary_issues.empty()) {
            return fmt::format("{} issue(s)", result.summary_issues.size());
        }
        return "failed";
    case Outcome::Blocked:
    case Outcome::Skip: return result.skip_reason;
    case Outcome::XFail:
    case Outcome::XPass: return result.xfail_reason;
    case Outcome::Pass: return {};
    }
    return {};
}

auto deferred_status_prefix(const RunResult &result, bool color_output) -> std::string {
    const auto status = async_live_status_text(async_live_status_for(result));
    if (color_output) {
        return fmt::format(fmt::fg(outcome_color(result.outcome)), "[ {:^9} ]", status);
    }
    return fmt::format("[ {:^9} ]", status);
}

auto deferred_case_line(std::string_view name, const RunResult &result, bool color_output) -> std::string {
    auto       line   = fmt::format("{} {}", deferred_status_prefix(result, color_output), name);
    const auto detail = async_live_detail_for(result);
    if (!detail.empty()) {
        line += fmt::format(" :: {}", detail);
    }
    line += fmt::format(" ({} ms)", duration_ms(result.time_s));
    return line;
}

void log_async_details(AsyncStatusRenderer &renderer, const RunResult &result) {
    if (result.outcome == Outcome::Fail) {
        if (!result.timeline.empty()) {
            for (const auto &line : result.timeline) {
                renderer.log(line);
            }
            renderer.log({});
            return;
        }
        for (const auto &issue : result.summary_issues) {
            renderer.log(issue);
        }
        if (!result.summary_issues.empty()) {
            renderer.log({});
        }
        return;
    }

    if (result.outcome == Outcome::XPass && !result.failures.empty()) {
        renderer.log(result.failures.front());
        renderer.log({});
        return;
    }

    if (result.outcome == Outcome::Pass && !result.timeline.empty()) {
        for (const auto &line : result.timeline) {
            renderer.log(line);
        }
        renderer.log({});
    }
}

} // namespace

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

bool run_tests_async_batch(TestRunContext &state, std::span<const gentest::Case> cases, std::span<const SuiteExecutionPlan> plans,
                           bool fail_fast, TestCounters &counters) {
    constexpr auto             kCanceledAsyncCleanupSoftTimeout = std::chrono::milliseconds(500);
    constexpr auto             kCanceledAsyncCleanupHardTimeout = std::chrono::milliseconds(2000);
    constexpr std::string_view kCanceledAsyncCleanupIssue       = "canceled async frame still has adopted work after cleanup timeout";
    std::vector<AsyncCaseRun>  async_runs;
    AsyncStatusRenderer        renderer(std::cout, AsyncStatusRenderer::terminal_mode(state.color_output), state.color_output);
    TestRunContext             final_state = state;
    final_state.suppress_case_output       = renderer.enabled();

    const auto finalize_run = [&](std::size_t run_index) {
        auto &run = async_runs[run_index];
        if (run.finalized) {
            return;
        }
        run.finalized = true;
        ++counters.total;
        auto      inv = finish_async_run(run);
        RunResult rr  = finish_invoke_result(final_state, cases[run.case_index], inv, counters);
        if (renderer.enabled()) {
            renderer.mark_final(run_index, async_live_status_for(rr), async_live_detail_for(rr), duration_ms(rr.time_s));
            renderer.log(deferred_case_line(cases[run.case_index].name, rr, final_state.color_output));
            log_async_details(renderer, rr);
        }
        if (state.acc) {
            gentest::runner::record_case_result(*state.acc, cases[run.case_index], std::move(rr), state.record_results);
        }
    };

    BatchAsyncScheduler scheduler(async_runs, renderer.enabled() ? &renderer : nullptr);

    const auto  should_stop            = [&] { return fail_fast && (counters.failures > 0 || counters.blocked > 0); };
    std::size_t first_unfinalized_scan = 0;

    const auto advance_first_unfinalized = [&] {
        while (first_unfinalized_scan < async_runs.size() && async_runs[first_unfinalized_scan].finalized) {
            ++first_unfinalized_scan;
        }
    };

    const auto has_adopted_work = [](const AsyncCaseRun &run) {
        return run.ctxinfo && run.ctxinfo->adopted_contexts.load(std::memory_order_acquire) != 0;
    };

    struct DeferredCanceledTask {
        gentest::detail::AsyncTaskPtr                     task;
        std::shared_ptr<gentest::detail::TestContextInfo> ctx;
        std::size_t                                       run_index        = 0;
        bool                                              cleanup_reported = false;
    };
    std::vector<DeferredCanceledTask> deferred_canceled_tasks;

    const auto adopted_contexts_released = [](const std::shared_ptr<gentest::detail::TestContextInfo> &ctx) {
        return !ctx || ctx->adopted_contexts.load(std::memory_order_acquire) == 0;
    };

    const auto wait_for_adopted_contexts_released = [&](const std::shared_ptr<gentest::detail::TestContextInfo> &ctx,
                                                        std::chrono::milliseconds                                timeout) {
        if (!ctx) {
            return true;
        }
        std::unique_lock<std::mutex> lk(ctx->adopted_mtx);
        return ctx->adopted_cv.wait_for(lk, timeout, [&] { return ctx->adopted_contexts.load(std::memory_order_acquire) == 0; });
    };

    const auto record_context_failure = [](const std::shared_ptr<gentest::detail::TestContextInfo> &ctx, std::string_view issue) {
        if (!ctx) {
            return;
        }
        {
            std::lock_guard<std::mutex> lk(ctx->mtx);
            ctx->failures.emplace_back(issue);
            ctx->failure_locations.push_back({});
            ctx->event_lines.emplace_back(issue);
            ctx->event_kinds.push_back('F');
        }
        ctx->has_failures.store(true, std::memory_order_release);
    };

    const auto destroy_released_canceled_tasks = [&](std::chrono::milliseconds wait_budget) {
        for (auto &deferred : deferred_canceled_tasks) {
            if (!deferred.task) {
                continue;
            }
            if (!adopted_contexts_released(deferred.ctx) && !wait_for_adopted_contexts_released(deferred.ctx, wait_budget)) {
                continue;
            }
            deferred.task.reset();
        }
    };

    const auto report_slow_canceled_tasks = [&] {
        for (auto &deferred : deferred_canceled_tasks) {
            if (!deferred.task || deferred.cleanup_reported) {
                continue;
            }
            deferred.cleanup_reported = true;
            auto       &run           = async_runs[deferred.run_index];
            const auto &test          = cases[run.case_index];
            gentest::detail::flush_current_buffer_for(run.ctxinfo.get());
            run.end = std::chrono::steady_clock::now();

            RunResult rr;
            rr.outcome = Outcome::Fail;
            rr.time_s  = std::chrono::duration<double>(run.end - run.start).count();
            if (run.ctxinfo) {
                rr.logs     = run.ctxinfo->logs;
                rr.timeline = run.ctxinfo->event_lines;
            }
            const std::string issue{kCanceledAsyncCleanupIssue};
            rr.failures.push_back(issue);
            rr.summary_issues.push_back(issue);

            ++counters.total;
            ++counters.failed;
            ++counters.failures;

            if (renderer.enabled()) {
                renderer.mark_final(deferred.run_index, AsyncLiveStatus::Fail, issue, duration_ms(rr.time_s));
                renderer.log(deferred_case_line(test.name, rr, final_state.color_output));
                log_async_details(renderer, rr);
            }
            if (state.acc) {
                gentest::runner::add_error_annotation(*state.acc, test.file, test.line, test.name, issue);
                gentest::runner::record_case_result(*state.acc, test, std::move(rr), state.record_results);
            }
        }
    };

    const auto release_still_blocked_canceled_tasks = [&] {
        for (auto &deferred : deferred_canceled_tasks) {
            if (!deferred.task) {
                continue;
            }
            if (!deferred.cleanup_reported) {
                report_slow_canceled_tasks();
            }
            // Destroying the coroutine frame can join user-owned threads that are waiting for a queued resume.
            auto *leaked_task = deferred.task.release();
            (void)leaked_task;
        }
        deferred_canceled_tasks.clear();
    };

    const auto finalize_fail_fast_stopped_adopted_run = [&](std::size_t run_index) {
        auto &run = async_runs[run_index];
        if (run.finalized) {
            return;
        }

        scheduler.cancel_owner(run_index);
        run.finalized = true;
        ++counters.total;

        classify_async_exception(run);
        gentest::runner::detail::cancel_active_test_context_without_wait(run.ctxinfo);
        gentest::detail::flush_current_buffer_for(run.ctxinfo.get());

        if (!adopted_contexts_released(run.ctxinfo) && !wait_for_adopted_contexts_released(run.ctxinfo, kCanceledAsyncCleanupSoftTimeout)) {
            record_context_failure(run.ctxinfo, kCanceledAsyncCleanupIssue);
        }

        if (!adopted_contexts_released(run.ctxinfo)) {
            (void)wait_for_adopted_contexts_released(run.ctxinfo, kCanceledAsyncCleanupHardTimeout - kCanceledAsyncCleanupSoftTimeout);
        }

        if (adopted_contexts_released(run.ctxinfo)) {
            run.task.reset();
            if (run.ctxinfo) {
                gentest::detail::close_canceled_context_if_released(*run.ctxinfo);
            }
        } else if (run.task) {
            // Match pending cancellation: after the hard timeout, avoid running
            // remaining frame-destruction cleanup while adopted work is still outstanding.
            auto *leaked_task = run.task.release();
            (void)leaked_task;
        }

        gentest::detail::flush_current_buffer_for(run.ctxinfo.get());
        run.end = std::chrono::steady_clock::now();

        InvokeResult inv;
        inv.ctxinfo   = run.ctxinfo;
        inv.exception = run.exception;
        inv.message   = std::move(run.message);
        inv.elapsed_s = std::chrono::duration<double>(run.end - run.start).count();

        RunResult rr = finish_invoke_result(final_state, cases[run.case_index], inv, counters);
        if (renderer.enabled()) {
            renderer.mark_final(run_index, async_live_status_for(rr), async_live_detail_for(rr), duration_ms(rr.time_s));
            renderer.log(deferred_case_line(cases[run.case_index].name, rr, final_state.color_output));
            log_async_details(renderer, rr);
        }
        if (state.acc) {
            gentest::runner::record_case_result(*state.acc, cases[run.case_index], std::move(rr), state.record_results);
        }
    };

    const auto finalize_completed_runs = [&] {
        for (std::size_t run_index = first_unfinalized_scan; run_index < async_runs.size(); ++run_index) {
            auto &run = async_runs[run_index];
            if (run.finalized || !run.ready_to_finalize) {
                continue;
            }
            const bool force_stop_result = fail_fast && has_adopted_work(run) && async_run_would_stop_fail_fast(run);
            if (has_adopted_work(run) && !force_stop_result) {
                continue;
            }
            if (force_stop_result) {
                finalize_fail_fast_stopped_adopted_run(run_index);
            } else {
                finalize_run(run_index);
            }
            if (should_stop()) {
                advance_first_unfinalized();
                return true;
            }
        }
        advance_first_unfinalized();
        return should_stop();
    };

    const auto pump_async = [&] {
        if (finalize_completed_runs() || should_stop()) {
            return true;
        }
        if (fail_fast) {
            while (scheduler.run_one_ready()) {
                if (finalize_completed_runs() || should_stop()) {
                    return true;
                }
            }
            return finalize_completed_runs() || should_stop();
        }
        scheduler.run_ready();
        return finalize_completed_runs() || should_stop();
    };

    const auto finish_pending_async_group = [&] {
        if (finalize_completed_runs()) {
            return true;
        }
        const bool stopped_while_draining =
            scheduler.finish_unresumable(fail_fast ? BatchAsyncScheduler::StopCallback(should_stop) : BatchAsyncScheduler::StopCallback{},
                                         [&] { return finalize_completed_runs(); });
        if (finalize_completed_runs()) {
            return true;
        }
        if (stopped_while_draining) {
            advance_first_unfinalized();
            return true;
        }
        for (std::size_t run_index = first_unfinalized_scan; run_index < async_runs.size(); ++run_index) {
            scheduler.cancel_owner(run_index);
            finalize_run(run_index);
            if (should_stop()) {
                advance_first_unfinalized();
                return true;
            }
        }
        advance_first_unfinalized();
        return should_stop();
    };

    const auto cancel_pending_async_group = [&] {
        for (std::size_t run_index = first_unfinalized_scan; run_index < async_runs.size(); ++run_index) {
            auto &run = async_runs[run_index];
            if (run.finalized) {
                continue;
            }
            const bool has_adopted_work = run.ctxinfo && run.ctxinfo->adopted_contexts.load(std::memory_order_acquire) != 0 && run.task &&
                                          run.task->handle() && !run.task->handle().done();
            scheduler.cancel_owner(run_index);
            gentest::runner::detail::cancel_active_test_context_without_wait(run.ctxinfo);
            gentest::detail::flush_current_buffer_for(run.ctxinfo.get());
            if (has_adopted_work) {
                deferred_canceled_tasks.push_back(
                    DeferredCanceledTask{.task = std::move(run.task), .ctx = run.ctxinfo, .run_index = run_index});
            }
            run.finalized = true;
        }
        destroy_released_canceled_tasks(kCanceledAsyncCleanupSoftTimeout);
        report_slow_canceled_tasks();
        destroy_released_canceled_tasks(kCanceledAsyncCleanupHardTimeout - kCanceledAsyncCleanupSoftTimeout);
        release_still_blocked_canceled_tasks();
        while (first_unfinalized_scan < async_runs.size() && async_runs[first_unfinalized_scan].finalized) {
            ++first_unfinalized_scan;
        }
    };

    const auto handle_case = [&](std::size_t i, void *ctx) {
        if (pump_async()) {
            return true;
        }
        const auto &test = cases[i];
        if (test.should_skip) {
            RunResult rr = make_static_skip_result(renderer.enabled() ? final_state : state, test, counters);
            if (renderer.enabled()) {
                renderer.log(deferred_case_line(test.name, rr, final_state.color_output));
                log_async_details(renderer, rr);
            }
            if (state.acc) {
                gentest::runner::record_case_result(*state.acc, test, std::move(rr), state.record_results);
            }
            return pump_async();
        }
        if (test.async_fn) {
            const auto run_index = async_runs.size();
            schedule_async_case(async_runs, test, i, ctx);
            renderer.add_case(run_index, test.name);
            auto &run = async_runs[run_index];
            if (run.task && run.exception == InvokeException::None) {
                scheduler.add_top_level(run_index, *run.task);
            } else {
                finalize_run(run_index);
            }
            return pump_async();
        }
        if (renderer.enabled()) {
            RunResult rr = execute_one(final_state, test, ctx, counters);
            renderer.log(deferred_case_line(test.name, rr, final_state.color_output));
            log_async_details(renderer, rr);
            if (state.acc) {
                gentest::runner::record_case_result(*state.acc, test, std::move(rr), state.record_results);
            }
        } else {
            execute_and_record(state, test, ctx, counters);
        }
        return pump_async();
    };

    bool stop_requested = false;
    for (const auto &plan : plans) {
        for (auto i : plan.free_like) {
            if (handle_case(i, nullptr)) {
                stop_requested = true;
                break;
            }
        }
        if (stop_requested) {
            break;
        }

        if (finish_pending_async_group()) {
            break;
        }

        const auto collect_groups = [&](const std::vector<gentest::runner::FixtureGroupPlan> &groups) {
            for (const auto &group : groups) {
                void       *group_ctx = nullptr;
                std::string group_reason;
                if (!group.idxs.empty() && !gentest::runner::acquire_case_fixture(cases[group.idxs.front()], group_ctx, group_reason)) {
                    const std::string msg = shared_fixture_unavailable_message(group.fixture, std::move(group_reason));
                    for (auto i : group.idxs) {
                        record_synthetic_skip(renderer.enabled() ? final_state : state, cases[i], msg, counters, true);
                        if (renderer.enabled()) {
                            RunResult rr;
                            rr.skipped     = true;
                            rr.outcome     = Outcome::Blocked;
                            rr.skip_reason = msg;
                            renderer.log(deferred_case_line(cases[i].name, rr, final_state.color_output));
                        }
                        if (should_stop()) {
                            return true;
                        }
                    }
                    continue;
                }

                for (auto i : group.idxs) {
                    if (handle_case(i, group_ctx)) {
                        return true;
                    }
                }

                if (finish_pending_async_group()) {
                    return true;
                }
            }
            return false;
        };

        if (collect_groups(plan.suite_groups)) {
            break;
        }
        if (collect_groups(plan.global_groups)) {
            break;
        }
    }

    if (!should_stop()) {
        (void)finish_pending_async_group();
    } else {
        cancel_pending_async_group();
    }

    renderer.finish();
    return should_stop();
}

} // namespace gentest::runner
