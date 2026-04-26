#include "runner_test_executor.h"

#include "gentest/detail/runtime_context.h"
#include "runner_async_status_renderer.h"
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
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gentest::runner {
namespace {

using Outcome   = gentest::runner::Outcome;
using RunResult = gentest::runner::RunResult;

constexpr std::string_view kAsyncCannotResumeMessage = "cannot resume, resume handle never created or lost";

long long duration_ms(double seconds) { return std::llround(seconds * 1000.0); }

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
    if (!state.suppress_case_output) {
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
        if (!state.suppress_case_output) {
            if (state.color_output) {
                fmt::print(fmt::fg(fmt::color::yellow), "[ BLOCKED ]");
                fmt::print(" {} :: {} ({} ms)\n", test.name, rr.skip_reason, dur_ms);
            } else {
                fmt::print("[ BLOCKED ] {} :: {} ({} ms)\n", test.name, rr.skip_reason, dur_ms);
            }
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
            if (!state.suppress_case_output) {
                if (state.color_output) {
                    fmt::print(fmt::fg(fmt::color::yellow), "[ BLOCKED ]");
                    fmt::print(" {} :: {} ({} ms)\n", test.name, issue, dur_ms);
                } else {
                    fmt::print("[ BLOCKED ] {} :: {} ({} ms)\n", test.name, issue, dur_ms);
                }
            }
            return rr;
        }

        ++c.skipped;
        rr.skipped        = true;
        rr.outcome        = Outcome::Skip;
        const auto dur_ms = duration_ms(rr.time_s);
        if (!state.suppress_case_output) {
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
            if (!state.suppress_case_output) {
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
            }
            return rr;
        }
        rr.outcome = Outcome::XPass;
        rr.failures.push_back(rr.xfail_reason.empty() ? "xpass" : fmt::format("xpass: {}", rr.xfail_reason));
        ++c.xpass;
        ++c.failed;
        ++c.failures;
        const auto dur_ms = duration_ms(rr.time_s);
        if (!state.suppress_case_output) {
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
        }
        if (!state.suppress_case_output) {
            fmt::print(stderr, "{}\n\n", rr.failures.front());
        }
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
        if (!state.suppress_case_output) {
            if (state.color_output) {
                fmt::print(stderr, fmt::fg(fmt::color::red), "[ FAIL ]");
                fmt::print(stderr, " {} :: {} issue(s) ({} ms)\n", test.name, ctxinfo->failures.size(), dur_ms);
            } else {
                fmt::print(stderr, "[ FAIL ] {} :: {} issue(s) ({} ms)\n", test.name, ctxinfo->failures.size(), dur_ms);
            }
        }
        std::size_t              failure_printed = 0;
        std::vector<std::string> failure_lines;
        for (std::size_t i = 0; i < ctxinfo->event_lines.size(); ++i) {
            const char  kind = (i < ctxinfo->event_kinds.size() ? ctxinfo->event_kinds[i] : 'L');
            const auto &ln   = ctxinfo->event_lines[i];
            if (kind == 'F') {
                if (!state.suppress_case_output) {
                    fmt::print(stderr, "{}\n", ln);
                }
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
                if (!state.suppress_case_output) {
                    fmt::print(stderr, "{}\n", ln);
                }
            }
        }
        if (!state.suppress_case_output) {
            fmt::print(stderr, "\n");
        }
        if (failure_lines.empty() && !ctxinfo->failures.empty())
            failure_lines.push_back(ctxinfo->failures.front());
        rr.summary_issues = std::move(failure_lines);
    } else if (!threw_non_skip) {
        const auto dur_ms = duration_ms(rr.time_s);
        if (!state.suppress_case_output) {
            if (state.color_output) {
                fmt::print(fmt::fg(fmt::color::green), "[ PASS ]");
                fmt::print(" {} ({} ms)\n", test.name, dur_ms);
            } else {
                fmt::print("[ PASS ] {} ({} ms)\n", test.name, dur_ms);
            }
        }
        rr.timeline = collect_pass_visible_timeline(ctxinfo);
        if (!state.suppress_case_output) {
            for (const auto &ln : rr.timeline) {
                fmt::print("{}\n", ln);
            }
            if (!rr.timeline.empty())
                fmt::print("\n");
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
        if (!state.suppress_case_output) {
            if (state.color_output) {
                fmt::print(stderr, fmt::fg(fmt::color::red), "[ FAIL ]");
                fmt::print(stderr, " {} ({} ms)\n", test.name, dur_ms);
            } else {
                fmt::print(stderr, "[ FAIL ] {} ({} ms)\n", test.name, dur_ms);
            }
            fmt::print(stderr, "\n");
        }
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
    bool                                              finalized = false;
};

class BatchAsyncScheduler final : public gentest::detail::AsyncScheduler {
  public:
    using CompletionCallback = std::function<void(std::size_t)>;

    BatchAsyncScheduler(std::vector<AsyncCaseRun> &runs, AsyncStatusRenderer *renderer, CompletionCallback completion_callback = {})
        : runs_(runs), renderer_(renderer), completion_callback_(std::move(completion_callback)) {}

    void post(std::coroutine_handle<> handle) override {
        if (!handle || handle.done()) {
            return;
        }
        blocked_handles_.erase(handle.address());
        ready_.push_back(handle);
    }

    void block(std::coroutine_handle<> handle, std::string reason) override { block_at(handle, std::move(reason), std::source_location{}); }

    void block_at(std::coroutine_handle<> handle, std::string reason, const std::source_location &loc) override {
        if (!handle) {
            return;
        }
        const auto owner = owner_for(handle);
        blocked_handles_[handle.address()] =
            BlockedHandle{.owner  = owner,
                          .reason = reason.empty() ? std::string("async test cannot resume") : std::move(reason),
                          .file   = renderer_ && loc.file_name() != nullptr ? std::string(loc.file_name()) : std::string{},
                          .line   = renderer_ ? loc.line() : 0};
    }

    void yield_at(std::coroutine_handle<> handle, const std::source_location &loc) override {
        if (!handle || handle.done()) {
            return;
        }
        if (renderer_) {
            const auto owner = owner_for(handle);
            blocked_handles_[handle.address()] =
                BlockedHandle{.owner  = owner,
                              .reason = "yielded cooperatively",
                              .file   = loc.file_name() == nullptr ? std::string{} : std::string(loc.file_name()),
                              .line   = loc.line()};
        }
        ready_.push_back(handle);
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

    void run_ready() {
        gentest::detail::AsyncSchedulerScope scheduler_scope(this);
        const auto                           ready_this_round = ready_.size();
        for (std::size_t ready_index = 0; ready_index < ready_this_round && !ready_.empty(); ++ready_index) {
            auto handle = ready_.front();
            ready_.pop_front();
            if (!handle || handle.done()) {
                continue;
            }
            const auto owner = owner_for(handle);
            if (owner >= runs_.size()) {
                continue;
            }
            auto &run = runs_[owner];
            if (run.finalized) {
                continue;
            }
            {
                gentest::runner::detail::CurrentTestAdoptionScope current_scope(run.ctxinfo);
                if (renderer_) {
                    renderer_->mark_running(owner);
                }
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

            if (run_is_complete(owner)) {
                complete(owner);
            } else if (renderer_ && run.exception == InvokeException::None) {
                const auto suspended = suspended_state_for(owner);
                renderer_->mark_suspended(owner, suspended.reason, suspended.file, suspended.line);
            }
        }
    }

    [[nodiscard]] auto has_ready() const noexcept -> bool { return !ready_.empty(); }

    void finish_unresumable() {
        while (!ready_.empty()) {
            run_ready();
        }
        for (std::size_t i = 0; i < runs_.size(); ++i) {
            auto &run = runs_[i];
            if (run.finalized || !run.task || !run.task->handle() || run.task->handle().done() || run.exception != InvokeException::None) {
                continue;
            }
            run.exception = InvokeException::Failure;
            run.message   = std::string(kAsyncCannotResumeMessage);
            {
                gentest::runner::detail::CurrentTestAdoptionScope current_scope(run.ctxinfo);
                gentest::detail::record_failure(run.message);
            }
            complete(i);
        }
    }

    void run() { finish_unresumable(); }

  private:
    struct BlockedHandle {
        std::size_t owner = 0;
        std::string reason;
        std::string file;
        unsigned    line = 0;
    };

    struct SuspendedState {
        std::string reason;
        std::string file;
        unsigned    line = 0;
    };

    [[nodiscard]] auto owner_for(std::coroutine_handle<> handle) const -> std::size_t {
        const auto it = owners_.find(handle.address());
        if (it == owners_.end()) {
            return runs_.size();
        }
        return it->second;
    }

    [[nodiscard]] auto suspended_state_for(std::size_t owner) const -> SuspendedState {
        for (const auto &entry : blocked_handles_) {
            const auto &blocked = entry.second;
            if (blocked.owner == owner && !blocked.reason.empty()) {
                return SuspendedState{.reason = blocked.reason, .file = blocked.file, .line = blocked.line};
            }
        }
        return SuspendedState{.reason = "waiting to resume"};
    }

    [[nodiscard]] bool run_is_complete(std::size_t owner) const {
        if (owner >= runs_.size()) {
            return false;
        }
        const auto &run = runs_[owner];
        return run.exception != InvokeException::None || !run.task || !run.task->handle() || run.task->handle().done();
    }

    void complete(std::size_t owner) {
        if (!completion_callback_ || owner >= runs_.size() || runs_[owner].finalized) {
            return;
        }
        completion_callback_(owner);
    }

    std::vector<AsyncCaseRun>                &runs_;
    AsyncStatusRenderer                      *renderer_ = nullptr;
    CompletionCallback                        completion_callback_;
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
        if (!state.suppress_case_output) {
            if (state.color_output) {
                fmt::print(fmt::fg(fmt::color::yellow), "[ BLOCKED ]");
                fmt::print(" {} :: {} ({} ms)\n", test.name, issue, dur_ms);
            } else {
                fmt::print("[ BLOCKED ] {} :: {} ({} ms)\n", test.name, issue, dur_ms);
            }
        }
    } else {
        ++c.skipped;
        if (!state.suppress_case_output) {
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

bool run_tests_async_batch(TestRunContext &state, std::span<const gentest::Case> cases, std::span<const SuiteExecutionPlan> plans,
                           bool fail_fast, TestCounters &counters) {
    std::vector<AsyncCaseRun> async_runs;
    AsyncStatusRenderer       renderer(std::cout, AsyncStatusRenderer::terminal_mode(state.color_output), state.color_output);
    TestRunContext            final_state = state;
    final_state.suppress_case_output      = renderer.enabled();

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

    BatchAsyncScheduler scheduler(async_runs, renderer.enabled() ? &renderer : nullptr, finalize_run);

    const auto should_stop = [&] { return fail_fast && (counters.failures > 0 || counters.blocked > 0); };

    const auto pump_async = [&] {
        do {
            scheduler.run_ready();
            if (should_stop()) {
                return true;
            }
        } while (fail_fast && scheduler.has_ready());
        return should_stop();
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

        const auto collect_groups = [&](const std::vector<gentest::runner::FixtureGroupPlan> &groups) {
            for (const auto &group : groups) {
                void       *group_ctx = nullptr;
                std::string group_reason;
                if (!group.idxs.empty() && !gentest::runner::acquire_case_fixture(cases[group.idxs.front()], group_ctx, group_reason)) {
                    const std::string msg =
                        group_reason.empty() ? std::string("fixture allocation returned null") : std::move(group_reason);
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

    scheduler.finish_unresumable();
    for (std::size_t run_index = 0; run_index < async_runs.size(); ++run_index) {
        finalize_run(run_index);
    }

    renderer.finish();
    return should_stop();
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
