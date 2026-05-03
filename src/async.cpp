#include "gentest/async.h"

#include "gentest/detail/runtime_context.h"
#include "gentest/detail/runtime_support.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <fmt/format.h>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace gentest::detail {
namespace {

thread_local AsyncScheduler *g_current_async_scheduler = nullptr;

enum class BlockingAsyncStatus {
    Completed,
    Blocked,
};

class BlockingAsyncScheduler final : public AsyncScheduler {
  public:
    explicit BlockingAsyncScheduler(std::shared_ptr<TestContextInfo> ctx) : ctx_(std::move(ctx)) {
        adopted_release_wake_ = std::make_shared<TestContextInfo::AdoptedReleaseWake>();
        register_adopted_release_wake(ctx_, adopted_release_wake_);
    }
    ~BlockingAsyncScheduler() override { deactivate(); }

    void post(std::coroutine_handle<> handle) override {
        if (!handle) {
            return;
        }
        {
            std::lock_guard<std::mutex> lk(mtx_);
            const auto                  owner_it = owners_.find(handle.address());
            if (owner_it == owners_.end()) {
                blocked_.erase(handle.address());
                return;
            }
            if (handle.done()) {
                blocked_.erase(handle.address());
                return;
            }
            blocked_.erase(handle.address());
            ready_.push_back(handle);
        }
        adopted_release_wake_->notify_one();
    }

    [[nodiscard]] auto make_waiter(std::coroutine_handle<> handle) -> WaiterTokenPtr override {
        auto token = AsyncScheduler::make_waiter(handle);
        if (!handle) {
            return token;
        }
        std::lock_guard<std::mutex> lk(mtx_);
        waiter_tokens_[handle.address()].push_back(token);
        return token;
    }

    void schedule_timer(std::chrono::steady_clock::time_point deadline, const WaiterTokenPtr &token) override {
        if (!token) {
            return;
        }
        {
            std::lock_guard<std::mutex> lk(mtx_);
            timers_.push_back(Timer{.deadline = deadline, .token = token});
        }
        adopted_release_wake_->notify_one();
    }

    void cancel_waiters(std::coroutine_handle<> handle) noexcept override {
        std::vector<WaiterTokenPtr> tokens;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            cancel_waiters_for_handle_locked(handle, tokens);
        }
        for (auto &token : tokens) {
            token->cancel();
        }
    }

    void block(std::coroutine_handle<> handle, std::string reason) override {
        if (!handle) {
            return;
        }
        std::lock_guard<std::mutex> lk(mtx_);
        blocked_[handle.address()] = reason.empty() ? std::string("async task cannot resume") : std::move(reason);
    }

    void attach_child(std::coroutine_handle<> child, std::coroutine_handle<> parent) override {
        if (!child || !parent) {
            return;
        }
        std::lock_guard<std::mutex> lk(mtx_);
        const auto                  parent_it = owners_.find(parent.address());
        if (parent_it != owners_.end()) {
            owners_[child.address()] = parent_it->second;
            children_[parent.address()].push_back(child);
        }
    }

    [[nodiscard]] auto run(AsyncTask &task, std::string &blocked_reason) -> BlockingAsyncStatus {
        AsyncSchedulerScope scheduler_scope(this);
        task.set_scheduler(this);
        {
            std::lock_guard<std::mutex> lk(mtx_);
            owners_[task.handle().address()] = 0;
        }
        post(task.handle());

        while (true) {
            while (true) {
                post_due_timers();
                auto handle = pop_ready();
                if (!handle) {
                    break;
                }
                if (handle.done()) {
                    continue;
                }
                auto previous = current_test();
                set_current_test(ctx_);
                try {
                    handle.resume();
                } catch (const std::exception &e) {
                    blocked_reason = fmt::format("async coroutine resume threw std::exception: {}", e.what());
                    set_current_test(std::move(previous));
                    return BlockingAsyncStatus::Blocked;
                } catch (...) {
                    blocked_reason = "async coroutine resume threw unknown exception";
                    set_current_test(std::move(previous));
                    return BlockingAsyncStatus::Blocked;
                }
                set_current_test(std::move(previous));
            }

            post_due_timers();
            if (!ready_empty()) {
                continue;
            }

            if (!task.handle() || task.handle().done()) {
                cancel_all_waiters();
                return BlockingAsyncStatus::Completed;
            }

            if ((!ctx_ || ctx_->adopted_contexts.load(std::memory_order_acquire) == 0) && !has_pending_timers()) {
                break;
            }

            wait_for_ready_or_adopted_release();
            if (!ready_empty()) {
                continue;
            }
        }

        if (!task.handle() || task.handle().done()) {
            cancel_all_waiters();
            return BlockingAsyncStatus::Completed;
        }

        const auto blocked_reason_snapshot = first_blocked_reason();
        if (!blocked_reason_snapshot.empty()) {
            blocked_reason = blocked_reason_snapshot;
        } else {
            blocked_reason = "async task cannot resume";
        }
        cancel_all_waiters();
        return BlockingAsyncStatus::Blocked;
    }

  private:
    struct Timer {
        std::chrono::steady_clock::time_point deadline;
        WaiterTokenPtr                        token;
    };

    void post_due_timers() {
        std::vector<WaiterTokenPtr> due;
        const auto                  now = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lk(mtx_);
            for (auto it = timers_.begin(); it != timers_.end();) {
                if (!it->token || !it->token->active()) {
                    it = timers_.erase(it);
                    continue;
                }
                if (it->deadline <= now) {
                    due.push_back(std::move(it->token));
                    it = timers_.erase(it);
                    continue;
                }
                ++it;
            }
        }
        for (auto &token : due) {
            token->post();
        }
    }

    [[nodiscard]] auto next_timer_deadline_locked() const -> std::optional<std::chrono::steady_clock::time_point> {
        std::optional<std::chrono::steady_clock::time_point> result;
        for (const auto &timer : timers_) {
            if (!timer.token || !timer.token->active()) {
                continue;
            }
            if (!result || timer.deadline < *result) {
                result = timer.deadline;
            }
        }
        return result;
    }

    [[nodiscard]] auto has_pending_timers() const -> bool {
        std::lock_guard<std::mutex> lk(mtx_);
        return next_timer_deadline_locked().has_value();
    }

    [[nodiscard]] auto pop_ready() -> std::coroutine_handle<> {
        std::lock_guard<std::mutex> lk(mtx_);
        if (ready_.empty()) {
            return {};
        }
        auto handle = ready_.front();
        ready_.pop_front();
        return handle;
    }

    [[nodiscard]] auto ready_empty() const -> bool {
        std::lock_guard<std::mutex> lk(mtx_);
        return ready_.empty();
    }

    [[nodiscard]] auto first_blocked_reason() const -> std::string {
        std::lock_guard<std::mutex> lk(mtx_);
        if (blocked_.empty()) {
            return {};
        }
        return blocked_.begin()->second;
    }

    void wait_for_ready_or_adopted_release() {
        std::unique_lock<std::mutex> lk(mtx_);
        const auto                   no_pending_wait = [&] {
            return (!ctx_ || ctx_->adopted_contexts.load(std::memory_order_acquire) == 0) && !next_timer_deadline_locked();
        };
        const auto ready_or_done_waiting = [&] { return !ready_.empty() || no_pending_wait(); };
        while (!ready_or_done_waiting()) {
            auto wake_deadline = next_timer_deadline_locked();
            if (!wake_deadline) {
                adopted_release_wake_->cv.wait(lk, ready_or_done_waiting);
                return;
            }
            if (adopted_release_wake_->cv.wait_until(lk, *wake_deadline, ready_or_done_waiting)) {
                return;
            }
            return;
        }
    }

    void cancel_all_waiters() {
        std::vector<WaiterTokenPtr> tokens;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            for (auto &[_, waiters] : waiter_tokens_) {
                for (auto &weak_waiter : waiters) {
                    if (auto waiter = weak_waiter.lock()) {
                        tokens.push_back(std::move(waiter));
                    }
                }
            }
            waiter_tokens_.clear();
            blocked_.clear();
            owners_.clear();
            children_.clear();
            ready_.clear();
            timers_.clear();
        }
        for (auto &token : tokens) {
            token->cancel();
        }
    }

    void remove_child_references_locked(void *address) {
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

    void cancel_one_handle_locked(std::coroutine_handle<> handle, std::vector<WaiterTokenPtr> &tokens) {
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
        blocked_.erase(address);
        owners_.erase(address);
        std::erase(ready_, handle);
    }

    void cancel_waiters_for_handle_locked(std::coroutine_handle<> handle, std::vector<WaiterTokenPtr> &tokens) {
        if (!handle) {
            return;
        }
        cancel_one_handle_locked(handle, tokens);
    }

    std::shared_ptr<TestContextInfo>                                    ctx_;
    mutable std::mutex                                                  mtx_;
    std::shared_ptr<TestContextInfo::AdoptedReleaseWake>                adopted_release_wake_;
    std::deque<std::coroutine_handle<>>                                 ready_;
    std::unordered_map<void *, std::size_t>                             owners_;
    std::unordered_map<void *, std::vector<std::coroutine_handle<>>>    children_;
    std::unordered_map<void *, std::string>                             blocked_;
    std::unordered_map<void *, std::vector<std::weak_ptr<WaiterToken>>> waiter_tokens_;
    std::vector<Timer>                                                  timers_;
};

auto make_context(std::string_view label) -> std::shared_ptr<TestContextInfo> {
    auto ctx          = std::make_shared<TestContextInfo>();
    ctx->display_name = std::string(label);
    ctx->active       = true;
    return ctx;
}

auto first_failure_or_empty(const std::shared_ptr<TestContextInfo> &ctx) -> std::string {
    if (!ctx) {
        return {};
    }
    flush_current_buffer_for(ctx.get());
    return first_recorded_failure(ctx);
}

auto runtime_skip_reason_or_default(const std::shared_ptr<TestContextInfo> &ctx) -> std::string {
    if (!ctx) {
        return "skip requested";
    }
    std::lock_guard<std::mutex> lk(ctx->mtx);
    if (!ctx->runtime_skip_reason.empty()) {
        return ctx->runtime_skip_reason;
    }
    return "skip requested";
}

} // namespace

auto current_async_scheduler() noexcept -> AsyncScheduler * { return g_current_async_scheduler; }

auto set_current_async_scheduler(AsyncScheduler *scheduler) noexcept -> AsyncScheduler * {
    auto *previous            = g_current_async_scheduler;
    g_current_async_scheduler = scheduler;
    return previous;
}

auto run_async_task_blocking(async_test<void> task, std::string_view label, std::string &error_out) -> bool {
    error_out.clear();
    auto ctx = make_context(label);

    auto previous = current_test();
    set_current_test(ctx);
    auto task_ptr = make_async_task(std::move(task));
    set_current_test(std::move(previous));

    std::string            blocked_reason;
    BlockingAsyncScheduler scheduler(ctx);
    const auto             status = task_ptr ? scheduler.run(*task_ptr, blocked_reason) : BlockingAsyncStatus::Blocked;

    wait_for_adopted_contexts(ctx);
    ctx->active = false;
    flush_current_buffer_for(ctx.get());

    if (status != BlockingAsyncStatus::Completed) {
        error_out = blocked_reason.empty() ? std::string("async task cannot resume") : std::move(blocked_reason);
        return false;
    }

    if (auto ex = task_ptr->exception()) {
        try {
            std::rethrow_exception(ex);
        } catch (const blocked_exception &e) { error_out = e.reason(); } catch (const skip_exception &) {
            error_out = runtime_skip_reason_or_default(ctx);
        } catch (const gentest::assertion &e) {
            error_out = first_failure_or_empty(ctx);
            if (error_out.empty()) {
                error_out = e.message();
            }
        } catch (const gentest::failure &e) { error_out = fmt::format("std::exception: {}", e.what()); } catch (const std::exception &e) {
            error_out = fmt::format("std::exception: {}", e.what());
        } catch (...) { error_out = "unknown exception"; }
    }

    if (error_out.empty()) {
        error_out = first_failure_or_empty(ctx);
    }
    return error_out.empty();
}

} // namespace gentest::detail
