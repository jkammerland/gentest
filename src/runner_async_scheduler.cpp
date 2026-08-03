#include "runner_async_scheduler.h"

#include "gentest/detail/runtime_context.h"
#include "runner_context_scope.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fmt/format.h>
#include <iterator>
#include <ranges>
#include <string_view>
#include <utility>

namespace gentest::runner {
namespace {

constexpr std::string_view kAsyncCannotResumeMessage = "cannot resume async test";

auto suspend_location_text(std::string_view file, unsigned line) -> std::string {
    if (file.empty() || line == 0) {
        return {};
    }
    std::filesystem::path p{std::string(file)};
    p                     = p.lexically_normal();
    std::string s         = p.generic_string();
    auto        keep_from = [&](std::string_view marker) -> bool {
        const std::size_t pos = s.find(marker);
        if (pos != std::string::npos) {
            s = s.substr(pos);
            return true;
        }
        return false;
    };
    (void)(keep_from("tests/") || keep_from("include/") || keep_from("src/") || keep_from("tools/"));
    return fmt::format("{}:{}", s, line);
}

} // namespace

BatchAsyncScheduler::BatchAsyncScheduler(std::vector<AsyncCaseRun> &runs, AsyncStatusRenderer *renderer)
    : runs_(runs), renderer_(renderer), adopted_release_wake_(std::make_shared<gentest::detail::TestContextInfo::AdoptedReleaseWake>()),
      core_([this] { adopted_release_wake_->notify_one(); }) {}

BatchAsyncScheduler::~BatchAsyncScheduler() { deactivate(); }

void BatchAsyncScheduler::post(std::coroutine_handle<> handle) { core_.post(handle); }

void BatchAsyncScheduler::post_frame(const gentest::detail::AsyncFramePtr &frame) { core_.post(frame); }

void BatchAsyncScheduler::block(std::coroutine_handle<> handle, std::string reason) { core_.block(handle, std::move(reason)); }

void BatchAsyncScheduler::block_at(std::coroutine_handle<> handle, std::string reason, const std::source_location &loc) {
    core_.block(handle, std::move(reason), loc);
}

void BatchAsyncScheduler::yield_at(std::coroutine_handle<> handle, const std::source_location &loc) { core_.yield(handle, loc); }

void BatchAsyncScheduler::attach_child(std::coroutine_handle<> child, std::coroutine_handle<> parent) { core_.attach_child(child, parent); }

void BatchAsyncScheduler::attach_child_frame(const gentest::detail::AsyncFramePtr &child, std::coroutine_handle<> parent) {
    core_.attach_child(child, parent);
}

auto BatchAsyncScheduler::make_waiter(std::coroutine_handle<> handle) -> WaiterTokenPtr { return core_.make_waiter(handle, control()); }

void BatchAsyncScheduler::schedule_timer(std::chrono::steady_clock::time_point deadline, const WaiterTokenPtr &token) {
    core_.schedule_timer(deadline, token);
}

void BatchAsyncScheduler::cancel_waiters(std::coroutine_handle<> handle) noexcept { core_.cancel_waiters(handle); }

void BatchAsyncScheduler::add_top_level(std::size_t run_index, gentest::detail::AsyncTask &task) {
    register_adopted_release_wake_for(run_index);
    task.set_scheduler(this);
    core_.register_frame(run_index, task.frame());
    core_.post(task.frame());
}

auto BatchAsyncScheduler::run_one_ready() -> bool {
    gentest::detail::AsyncSchedulerScope scheduler_scope(this);
    core_.post_due_timers();
    return resume_one_ready();
}

void BatchAsyncScheduler::run_ready() {
    gentest::detail::AsyncSchedulerScope scheduler_scope(this);
    core_.post_due_timers();
    for (std::size_t remaining = core_.ready_size(); remaining != 0; --remaining) {
        core_.post_due_timers();
        if (!resume_one_ready()) {
            return;
        }
    }
}

auto BatchAsyncScheduler::has_ready() const noexcept -> bool { return core_.has_ready(); }

void BatchAsyncScheduler::cancel_owner(std::size_t owner) { core_.cancel_owner(owner); }

auto BatchAsyncScheduler::finish_unresumable(const StopCallback &should_stop, const ProgressCallback &after_progress) -> bool {
    if (drain_ready_and_adopted_work(should_stop, after_progress)) {
        return true;
    }
    for (std::size_t i = 0; i < runs_.size(); ++i) {
        if (after_progress && after_progress()) {
            return true;
        }
        if (should_stop && should_stop()) {
            return true;
        }
        auto &run = runs_[i];
        if (run.finalized || !run.task || !run.task->handle() || run.task->handle().done() || run.exception != InvokeException::None) {
            continue;
        }
        run.exception        = InvokeException::Failure;
        const auto suspended = core_.suspended_state_for(i);
        run.message          = format_cannot_resume_message(suspended);
        {
            gentest::runner::detail::CurrentTestContextScope current_scope(run.ctxinfo);
            if (!suspended.file.empty() && suspended.line != 0) {
                gentest::detail::record_failure_at(run.message, suspended.file, suspended.line);
            } else {
                gentest::detail::record_failure(run.message);
            }
            gentest::detail::run_context_cancel_hooks(run.ctxinfo);
        }
        complete(i);
        if (after_progress && after_progress()) {
            return true;
        }
        if (should_stop && should_stop()) {
            return true;
        }
    }
    return false;
}

void BatchAsyncScheduler::run() { (void)finish_unresumable(); }

auto BatchAsyncScheduler::owner_for(std::coroutine_handle<> handle) const -> std::size_t { return core_.owner_for(handle); }

void BatchAsyncScheduler::register_adopted_release_wake_for(std::size_t run_index) {
    if (run_index >= runs_.size() || !runs_[run_index].ctxinfo) {
        return;
    }
    auto *ctx = runs_[run_index].ctxinfo.get();
    if (!context_listener_contexts_.insert(ctx).second) {
        return;
    }
    gentest::detail::register_adopted_release_wake(runs_[run_index].ctxinfo, adopted_release_wake_);
}

auto BatchAsyncScheduler::format_cannot_resume_message(const AsyncSchedulerCore::SuspendedState &state) const -> std::string {
    std::string message(kAsyncCannotResumeMessage);
    const auto  location = suspend_location_text(state.file, state.line);
    if (!location.empty()) {
        fmt::format_to(std::back_inserter(message), "; suspended at {}", location);
    }
    if (state.sequence != 0 && !state.reason.empty()) {
        fmt::format_to(std::back_inserter(message), ": {}", state.reason);
    }
    return message;
}

bool BatchAsyncScheduler::run_is_complete(std::size_t owner) const {
    if (owner >= runs_.size()) {
        return false;
    }
    const auto &run = runs_[owner];
    return run.exception != InvokeException::None || !run.task || !run.task->handle() || run.task->handle().done();
}

void BatchAsyncScheduler::complete(std::size_t owner) {
    if (owner >= runs_.size() || runs_[owner].finalized || runs_[owner].ready_to_finalize) {
        return;
    }
    cancel_owner(owner);
    if (runs_[owner].ctxinfo) {
        gentest::runner::detail::request_active_test_context_stop(runs_[owner].ctxinfo);
    }
    runs_[owner].ready_to_finalize = true;
    adopted_release_wake_->notify_one();
}

auto BatchAsyncScheduler::resume_one_ready() -> bool {
    while (auto frame = core_.pop_ready()) {
        if (!frame || !frame->handle) {
            continue;
        }
        const auto owner = core_.owner_for(frame);
        if (owner >= runs_.size()) {
            continue;
        }
        if (frame->is_canceled() || frame->done()) {
            continue;
        }
        auto &run = runs_[owner];
        if (run.finalized) {
            continue;
        }
        {
            gentest::runner::detail::CurrentTestContextScope current_scope(run.ctxinfo);
            if (renderer_) {
                renderer_->mark_running(owner);
            }
            core_.clear_suspend_state(frame);
            try {
                frame->handle.resume();
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
            const auto suspended = core_.suspended_state_for(owner);
            if (suspended.kind == AsyncSchedulerCore::SuspendKind::Yielded) {
                renderer_->mark_yielded(owner, suspended.reason, suspended.file, suspended.line);
            } else {
                renderer_->mark_suspended(owner, suspended.reason, suspended.file, suspended.line);
            }
        }
        return true;
    }
    return false;
}

auto BatchAsyncScheduler::has_unfinished_adopted_work() const -> bool {
    return std::ranges::any_of(runs_, [](const AsyncCaseRun &run) {
        if (run.finalized || !run.ctxinfo || run.ctxinfo->adopted_contexts.load(std::memory_order_acquire) == 0) {
            return false;
        }
        if (run.ready_to_finalize) {
            return true;
        }
        return run.task && run.task->handle() && !run.task->handle().done() && run.exception == InvokeException::None;
    });
}

void BatchAsyncScheduler::wait_for_ready_or_adopted_release(const StopCallback &should_stop) {
    const auto stop_or_progress_locked = [&] {
        return core_.has_ready() || (!has_unfinished_adopted_work() && !core_.next_timer_deadline()) || (should_stop && should_stop());
    };

    while (true) {
        std::unique_lock<std::mutex>                         wake_lk(adopted_release_wake_->mtx);
        const auto                                           wake_generation = adopted_release_wake_->generation;
        std::optional<std::chrono::steady_clock::time_point> wake_deadline;
        if (stop_or_progress_locked()) {
            break;
        }
        wake_deadline = core_.next_timer_deadline();
        if (renderer_) {
            if (const auto renderer_deadline = renderer_->next_refresh_deadline();
                renderer_deadline && (!wake_deadline || *renderer_deadline < *wake_deadline)) {
                wake_deadline = renderer_deadline;
            }
        }
        const auto wake_observed = [&] { return adopted_release_wake_->generation != wake_generation; };
        if (wake_deadline) {
            (void)adopted_release_wake_->cv.wait_until(wake_lk, *wake_deadline, wake_observed);
        } else {
            adopted_release_wake_->cv.wait(wake_lk, wake_observed);
        }
        break;
    }
    if (renderer_) {
        renderer_->refresh_if_due();
    }
    core_.post_due_timers();
}

auto BatchAsyncScheduler::drain_ready_and_adopted_work(const StopCallback &should_stop, const ProgressCallback &after_progress) -> bool {
    do {
        core_.post_due_timers();
        while (has_ready()) {
            if (should_stop && should_stop()) {
                return true;
            }
            if (!run_one_ready()) {
                break;
            }
            if (after_progress && after_progress()) {
                return true;
            }
            if (should_stop && should_stop()) {
                return true;
            }
        }
        if (after_progress && after_progress()) {
            return true;
        }
        if (should_stop && should_stop()) {
            return true;
        }
        if (!has_unfinished_adopted_work() && !core_.has_pending_timers()) {
            return false;
        }
        wait_for_ready_or_adopted_release(should_stop);
    } while (true);
}

} // namespace gentest::runner
