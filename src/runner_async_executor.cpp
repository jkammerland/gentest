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
    std::vector<AsyncCaseRun> async_runs;
    AsyncStatusRenderer       renderer(std::cout, AsyncStatusRenderer::terminal_mode(state.color_output), state.color_output);
    TestRunContext            final_state = state;
    final_state.suppress_case_output      = renderer.enabled();

    const auto record_invoke_result = [&](std::size_t run_index, const InvokeResult &inv) {
        auto     &run = async_runs[run_index];
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

    const auto finalize_run = [&](std::size_t run_index) {
        auto &run = async_runs[run_index];
        if (run.finalized) {
            return;
        }
        run.finalized = true;
        ++counters.total;
        record_invoke_result(run_index, finish_async_run(run));
    };

    BatchAsyncScheduler scheduler(async_runs, renderer.enabled() ? &renderer : nullptr);

    bool        fail_fast_stop_requested = false;
    const auto  should_stop = [&] { return fail_fast && (fail_fast_stop_requested || counters.failures > 0 || counters.blocked > 0); };
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
        std::size_t                                       run_index                = 0;
        bool                                              record_result_on_cleanup = false;
        bool                                              result_recorded          = false;
    };
    std::vector<DeferredCanceledTask> deferred_canceled_tasks;

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

    const auto record_canceled_completed_result = [&](DeferredCanceledTask &deferred) {
        if (deferred.result_recorded) {
            return;
        }
        deferred.result_recorded = true;

        auto &run = async_runs[deferred.run_index];
        gentest::detail::flush_current_buffer_for(run.ctxinfo.get());
        run.end = std::chrono::steady_clock::now();

        InvokeResult inv;
        inv.ctxinfo   = run.ctxinfo;
        inv.exception = run.exception;
        inv.message   = std::move(run.message);
        inv.elapsed_s = std::chrono::duration<double>(run.end - run.start).count();

        ++counters.total;
        record_invoke_result(deferred.run_index, inv);
    };

    const auto destroy_canceled_tasks_after_adopted_release = [&] {
        for (auto &deferred : deferred_canceled_tasks) {
            if (!deferred.task) {
                continue;
            }
            gentest::detail::wait_for_adopted_contexts(deferred.ctx);
            deferred.task.reset();
            if (deferred.ctx) {
                gentest::detail::close_canceled_context_if_released(*deferred.ctx);
            }
            if (deferred.record_result_on_cleanup) {
                record_canceled_completed_result(deferred);
            }
        }
        deferred_canceled_tasks.clear();
    };

    const auto finalize_canceled_adopted_run = [&](std::size_t run_index, bool request_stop) {
        auto &run = async_runs[run_index];
        if (run.finalized) {
            return;
        }

        scheduler.cancel_owner(run_index);
        run.finalized = true;
        if (request_stop) {
            fail_fast_stop_requested = true;
        }
        ++counters.total;

        classify_async_exception(run);
        gentest::runner::detail::cancel_active_test_context_without_wait(run.ctxinfo);
        gentest::detail::flush_current_buffer_for(run.ctxinfo.get());

        gentest::detail::wait_for_adopted_contexts(run.ctxinfo);
        run.task.reset();
        if (run.ctxinfo) {
            gentest::detail::close_canceled_context_if_released(*run.ctxinfo);
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
                finalize_canceled_adopted_run(run_index, true);
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
        const BatchAsyncScheduler::StopCallback drain_should_stop =
            fail_fast ? BatchAsyncScheduler::StopCallback(should_stop) : BatchAsyncScheduler::StopCallback{};
        const bool stopped_while_draining = scheduler.finish_unresumable(drain_should_stop, [&] { return finalize_completed_runs(); });
        if (finalize_completed_runs()) {
            return true;
        }
        if (stopped_while_draining && fail_fast) {
            advance_first_unfinalized();
            return true;
        }
        for (std::size_t run_index = first_unfinalized_scan; run_index < async_runs.size(); ++run_index) {
            auto &run = async_runs[run_index];
            if (run.finalized) {
                continue;
            }
            if (has_adopted_work(run)) {
                if (run.ready_to_finalize) {
                    classify_async_exception(run);
                } else if (run.exception == InvokeException::None) {
                    run.exception = InvokeException::Failure;
                    run.message   = "async test canceled before completion";
                    record_context_failure(run.ctxinfo, run.message);
                }
                scheduler.cancel_owner(run_index);
                gentest::runner::detail::cancel_active_test_context_without_wait(run.ctxinfo);
                gentest::detail::flush_current_buffer_for(run.ctxinfo.get());
                deferred_canceled_tasks.push_back(DeferredCanceledTask{.task                     = std::move(run.task),
                                                                       .ctx                      = run.ctxinfo,
                                                                       .run_index                = run_index,
                                                                       .record_result_on_cleanup = true});
                run.finalized = true;
            } else {
                scheduler.cancel_owner(run_index);
                if (!run.ready_to_finalize) {
                    if (run.exception == InvokeException::None) {
                        run.exception = InvokeException::Failure;
                        run.message   = "async test canceled before completion";
                        record_context_failure(run.ctxinfo, run.message);
                    }
                    gentest::runner::detail::cancel_active_test_context_without_wait(run.ctxinfo);
                    gentest::detail::flush_current_buffer_for(run.ctxinfo.get());
                }
                finalize_run(run_index);
            }
            if (should_stop()) {
                advance_first_unfinalized();
                return true;
            }
        }
        destroy_canceled_tasks_after_adopted_release();
        advance_first_unfinalized();
        return should_stop();
    };

    const auto cancel_pending_async_group = [&] {
        for (std::size_t run_index = first_unfinalized_scan; run_index < async_runs.size(); ++run_index) {
            auto &run = async_runs[run_index];
            if (run.finalized) {
                continue;
            }
            const bool adopted_work = has_adopted_work(run);
            if (adopted_work && run.ready_to_finalize) {
                classify_async_exception(run);
            }
            scheduler.cancel_owner(run_index);
            gentest::runner::detail::cancel_active_test_context_without_wait(run.ctxinfo);
            gentest::detail::flush_current_buffer_for(run.ctxinfo.get());
            if (adopted_work) {
                deferred_canceled_tasks.push_back(DeferredCanceledTask{.task                     = std::move(run.task),
                                                                       .ctx                      = run.ctxinfo,
                                                                       .run_index                = run_index,
                                                                       .record_result_on_cleanup = run.ready_to_finalize});
            }
            run.finalized = true;
        }
        destroy_canceled_tasks_after_adopted_release();
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
