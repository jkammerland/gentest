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
    : runs_(runs), renderer_(renderer), adopted_release_wake_(std::make_shared<gentest::detail::TestContextInfo::AdoptedReleaseWake>()) {}

BatchAsyncScheduler::~BatchAsyncScheduler() { deactivate(); }

void BatchAsyncScheduler::post(std::coroutine_handle<> handle) {
    if (!handle) {
        return;
    }
    bool should_notify = false;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        const auto                  owner = owner_for_locked(handle);
        if (owner == kInvalidOwner) {
            blocked_handles_.erase(handle.address());
            return;
        }
        if (handle.done()) {
            blocked_handles_.erase(handle.address());
            return;
        }
        blocked_handles_.erase(handle.address());
        ready_.push_back(handle);
        should_notify = true;
    }
    if (should_notify) {
        adopted_release_wake_->notify_one();
    }
}

void BatchAsyncScheduler::block(std::coroutine_handle<> handle, std::string reason) {
    block_at(handle, std::move(reason), std::source_location{});
}

void BatchAsyncScheduler::block_at(std::coroutine_handle<> handle, std::string reason, const std::source_location &loc) {
    if (!handle) {
        return;
    }
    std::lock_guard<std::mutex> lk(mtx_);
    const auto                  owner = owner_for_locked(handle);
    if (owner != kInvalidOwner) {
        blocked_handles_[handle.address()] =
            BlockedHandle{.owner    = owner,
                          .kind     = SuspendKind::Suspended,
                          .sequence = ++suspend_sequence_,
                          .reason   = reason.empty() ? std::string("async test cannot resume") : std::move(reason),
                          .file     = loc.file_name() == nullptr ? std::string{} : std::string(loc.file_name()),
                          .line     = loc.line()};
    }
}

void BatchAsyncScheduler::yield_at(std::coroutine_handle<> handle, const std::source_location &loc) {
    if (!handle || handle.done()) {
        return;
    }
    {
        std::lock_guard<std::mutex> lk(mtx_);
        const auto                  owner = owner_for_locked(handle);
        if (owner == kInvalidOwner) {
            return;
        }
        blocked_handles_[handle.address()] =
            BlockedHandle{.owner    = owner,
                          .kind     = SuspendKind::Yielded,
                          .sequence = ++suspend_sequence_,
                          .reason   = "yielded cooperatively",
                          .file     = loc.file_name() == nullptr ? std::string{} : std::string(loc.file_name()),
                          .line     = loc.line()};
        ready_.push_back(handle);
    }
    adopted_release_wake_->notify_one();
}

void BatchAsyncScheduler::attach_child(std::coroutine_handle<> child, std::coroutine_handle<> parent) {
    if (!child || !parent) {
        return;
    }
    std::lock_guard<std::mutex> lk(mtx_);
    const auto                  parent_owner = owner_for_locked(parent);
    if (parent_owner != kInvalidOwner) {
        owners_[child.address()] = parent_owner;
        children_[parent.address()].push_back(child);
    }
}

auto BatchAsyncScheduler::make_waiter(std::coroutine_handle<> handle) -> WaiterTokenPtr {
    auto token = AsyncScheduler::make_waiter(handle);
    if (!handle) {
        return token;
    }
    std::lock_guard<std::mutex> lk(mtx_);
    if (owner_for_locked(handle) == kInvalidOwner) {
        token->cancel();
        return token;
    }
    waiter_tokens_[handle.address()].push_back(token);
    return token;
}

void BatchAsyncScheduler::schedule_timer(std::chrono::steady_clock::time_point deadline, const WaiterTokenPtr &token) {
    if (!token) {
        return;
    }
    {
        std::lock_guard<std::mutex> lk(mtx_);
        timers_.push(deadline, token);
    }
    adopted_release_wake_->notify_one();
}

void BatchAsyncScheduler::cancel_waiters(std::coroutine_handle<> handle) noexcept {
    std::vector<WaiterTokenPtr> tokens;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        cancel_waiters_for_handle_locked(handle, tokens);
    }
    for (auto &token : tokens) {
        token->cancel();
    }
}

void BatchAsyncScheduler::add_top_level(std::size_t run_index, gentest::detail::AsyncTask &task) {
    register_adopted_release_wake_for(run_index);
    task.set_scheduler(this);
    {
        std::lock_guard<std::mutex> lk(mtx_);
        owners_[task.handle().address()] = run_index;
    }
    post(task.handle());
}

auto BatchAsyncScheduler::run_one_ready() -> bool {
    gentest::detail::AsyncSchedulerScope scheduler_scope(this);
    post_due_timers();
    return resume_one_ready();
}

void BatchAsyncScheduler::run_ready() {
    gentest::detail::AsyncSchedulerScope scheduler_scope(this);
    post_due_timers();
    for (std::size_t remaining = ready_size(); remaining != 0; --remaining) {
        post_due_timers();
        if (!resume_one_ready()) {
            return;
        }
    }
}

auto BatchAsyncScheduler::has_ready() const noexcept -> bool { return !ready_empty(); }

void BatchAsyncScheduler::cancel_owner(std::size_t owner) {
    std::vector<WaiterTokenPtr> tokens;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        cancel_owner_waiters_locked(owner, tokens);
    }
    for (auto &token : tokens) {
        token->cancel();
    }
}

auto BatchAsyncScheduler::finish_unresumable(const StopCallback &should_stop, const ProgressCallback &after_progress,
                                             const StopCallback &should_stop_waiting_for_adopted) -> bool {
    if (drain_ready_and_adopted_work(should_stop, after_progress, should_stop_waiting_for_adopted)) {
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
        const auto suspended = suspended_state_for(i);
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

auto BatchAsyncScheduler::owner_for(std::coroutine_handle<> handle) const -> std::size_t {
    std::lock_guard<std::mutex> lk(mtx_);
    return owner_for_locked(handle);
}

auto BatchAsyncScheduler::owner_for_locked(std::coroutine_handle<> handle) const -> std::size_t {
    const auto it = owners_.find(handle.address());
    if (it == owners_.end()) {
        return kInvalidOwner;
    }
    return it->second;
}

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

auto BatchAsyncScheduler::suspended_state_for(std::size_t owner) const -> SuspendedState {
    SuspendedState              result{.reason = "waiting to resume"};
    std::lock_guard<std::mutex> lk(mtx_);
    for (const auto &entry : blocked_handles_) {
        const auto &blocked = entry.second;
        if (blocked.owner == owner && !blocked.reason.empty() && blocked.sequence >= result.sequence) {
            result = SuspendedState{
                .kind     = blocked.kind,
                .reason   = blocked.reason,
                .file     = blocked.file,
                .line     = blocked.line,
                .sequence = blocked.sequence,
            };
        }
    }
    return result;
}

auto BatchAsyncScheduler::format_cannot_resume_message(const SuspendedState &state) const -> std::string {
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
    runs_[owner].ready_to_finalize = true;
    adopted_release_wake_->notify_one();
}

void BatchAsyncScheduler::cancel_owner_waiters_locked(std::size_t owner, std::vector<WaiterTokenPtr> &tokens) {
    std::vector<std::coroutine_handle<>> handles;
    for (const auto &[address, handle_owner] : owners_) {
        if (handle_owner == owner) {
            handles.push_back(std::coroutine_handle<>::from_address(address));
        }
    }
    for (auto handle : handles) {
        cancel_one_handle_locked(handle, tokens);
    }
}

void BatchAsyncScheduler::remove_child_references_locked(void *address) {
    for (auto it = children_.begin(); it != children_.end();) {
        auto &children = it->second;
        std::erase_if(children, [address](std::coroutine_handle<> child) { return child.address() == address; });
        if (children.empty()) {
            it = children_.erase(it);
        } else {
            ++it;
        }
    }
}

void BatchAsyncScheduler::cancel_one_handle_locked(std::coroutine_handle<> handle, std::vector<WaiterTokenPtr> &tokens) {
    const auto address = handle.address();
    if (const auto child_it = children_.find(address); child_it != children_.end()) {
        auto children = std::move(child_it->second);
        children_.erase(child_it);
        for (auto child : children) {
            cancel_one_handle_locked(child, tokens);
        }
    }
    remove_child_references_locked(address);

    const auto waiter_it = waiter_tokens_.find(address);
    if (waiter_it != waiter_tokens_.end()) {
        for (auto &weak_waiter : waiter_it->second) {
            if (auto waiter = weak_waiter.lock()) {
                tokens.push_back(std::move(waiter));
            }
        }
        waiter_tokens_.erase(waiter_it);
    }
    blocked_handles_.erase(address);
    owners_.erase(address);
    std::erase(ready_, handle);
}

void BatchAsyncScheduler::cancel_waiters_for_handle_locked(std::coroutine_handle<> handle, std::vector<WaiterTokenPtr> &tokens) {
    if (!handle) {
        return;
    }
    cancel_one_handle_locked(handle, tokens);
}

void BatchAsyncScheduler::post_due_timers() {
    std::vector<WaiterTokenPtr> due;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        due = timers_.pop_due(std::chrono::steady_clock::now());
    }
    for (auto &token : due) {
        token->post();
    }
}

auto BatchAsyncScheduler::next_timer_deadline_locked() -> std::optional<std::chrono::steady_clock::time_point> {
    return timers_.next_deadline();
}

auto BatchAsyncScheduler::has_pending_timers() -> bool {
    std::lock_guard<std::mutex> lk(mtx_);
    return timers_.has_pending();
}

auto BatchAsyncScheduler::ready_size() const -> std::size_t {
    std::lock_guard<std::mutex> lk(mtx_);
    return ready_.size();
}

auto BatchAsyncScheduler::ready_empty() const noexcept -> bool {
    std::lock_guard<std::mutex> lk(mtx_);
    return ready_.empty();
}

auto BatchAsyncScheduler::pop_ready() -> std::coroutine_handle<> {
    std::lock_guard<std::mutex> lk(mtx_);
    if (ready_.empty()) {
        return {};
    }
    auto handle = ready_.front();
    ready_.pop_front();
    return handle;
}

void BatchAsyncScheduler::clear_suspend_state(std::coroutine_handle<> handle) {
    if (!handle) {
        return;
    }
    std::lock_guard<std::mutex> lk(mtx_);
    blocked_handles_.erase(handle.address());
}

auto BatchAsyncScheduler::resume_one_ready() -> bool {
    while (auto handle = pop_ready()) {
        if (!handle) {
            continue;
        }
        const auto owner = owner_for(handle);
        if (owner >= runs_.size()) {
            continue;
        }
        if (handle.done()) {
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
            clear_suspend_state(handle);
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
            if (suspended.kind == SuspendKind::Yielded) {
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

void BatchAsyncScheduler::wait_for_ready_or_adopted_release(const StopCallback &should_stop,
                                                            const StopCallback &should_stop_waiting_for_adopted) {
    const auto stop_or_progress_locked = [&] {
        return !ready_.empty() || (!has_unfinished_adopted_work() && !next_timer_deadline_locked()) || (should_stop && should_stop()) ||
               (should_stop_waiting_for_adopted && should_stop_waiting_for_adopted());
    };

    while (true) {
        std::unique_lock<std::mutex>                         wake_lk(adopted_release_wake_->mtx);
        const auto                                           wake_generation = adopted_release_wake_->generation;
        std::optional<std::chrono::steady_clock::time_point> wake_deadline;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (stop_or_progress_locked()) {
                break;
            }
            wake_deadline = next_timer_deadline_locked();
        }
        const auto wake_observed = [&] { return adopted_release_wake_->generation != wake_generation; };
        if (wake_deadline) {
            (void)adopted_release_wake_->cv.wait_until(wake_lk, *wake_deadline, wake_observed);
        } else {
            adopted_release_wake_->cv.wait(wake_lk, wake_observed);
        }
        break;
    }
    post_due_timers();
}

auto BatchAsyncScheduler::drain_ready_and_adopted_work(const StopCallback &should_stop, const ProgressCallback &after_progress,
                                                       const StopCallback &should_stop_waiting_for_adopted) -> bool {
    do {
        post_due_timers();
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
        if (!has_unfinished_adopted_work() && !has_pending_timers()) {
            return false;
        }
        if (should_stop_waiting_for_adopted && should_stop_waiting_for_adopted()) {
            return true;
        }
        wait_for_ready_or_adopted_release(should_stop, should_stop_waiting_for_adopted);
    } while (true);
}

} // namespace gentest::runner
