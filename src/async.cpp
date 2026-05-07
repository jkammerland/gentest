#include "gentest/async.h"

#include "async_scheduler_core.h"
#include "gentest/detail/runtime_context.h"
#include "gentest/detail/runtime_support.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <fmt/format.h>
#include <mutex>

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
        core_.set_wake_callback([this] { adopted_release_wake_->notify_one(); });
    }
    ~BlockingAsyncScheduler() override { deactivate(); }

    void post(std::coroutine_handle<> handle) override { core_.post(handle); }

    void post_frame(const AsyncFramePtr &frame) override { core_.post(frame); }

    [[nodiscard]] auto make_waiter(std::coroutine_handle<> handle) -> WaiterTokenPtr override {
        return core_.make_waiter(handle, control());
    }

    void schedule_timer(std::chrono::steady_clock::time_point deadline, const WaiterTokenPtr &token) override {
        core_.schedule_timer(deadline, token);
    }

    void cancel_waiters(std::coroutine_handle<> handle) noexcept override { core_.cancel_waiters(handle); }

    void block(std::coroutine_handle<> handle, std::string reason) override { core_.block(handle, std::move(reason)); }

    void attach_child(std::coroutine_handle<> child, std::coroutine_handle<> parent) override { core_.attach_child(child, parent); }

    void attach_child_frame(const AsyncFramePtr &child, std::coroutine_handle<> parent) override { core_.attach_child(child, parent); }

    [[nodiscard]] auto run(AsyncTask &task, std::string &blocked_reason) -> BlockingAsyncStatus {
        AsyncSchedulerScope scheduler_scope(this);
        task.set_scheduler(this);
        core_.register_frame(0, task.frame());
        core_.post(task.frame());

        while (true) {
            while (true) {
                core_.post_due_timers();
                auto frame = core_.pop_ready();
                if (!frame || !frame->handle) {
                    break;
                }
                if (frame->is_canceled() || frame->done()) {
                    continue;
                }
                auto previous = current_test();
                set_current_test(ctx_);
                try {
                    core_.clear_suspend_state(frame);
                    frame->handle.resume();
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

            core_.post_due_timers();
            if (core_.has_ready()) {
                continue;
            }

            if (!task.handle() || task.handle().done()) {
                cancel_all_waiters();
                return BlockingAsyncStatus::Completed;
            }

            if ((!ctx_ || ctx_->adopted_contexts.load(std::memory_order_acquire) == 0) && !core_.has_pending_timers()) {
                break;
            }

            wait_for_ready_or_adopted_release();
            if (core_.has_ready()) {
                continue;
            }
        }

        if (!task.handle() || task.handle().done()) {
            cancel_all_waiters();
            return BlockingAsyncStatus::Completed;
        }

        const auto blocked_reason_snapshot = core_.first_blocked_reason();
        if (!blocked_reason_snapshot.empty()) {
            blocked_reason = blocked_reason_snapshot;
        } else {
            blocked_reason = "async task cannot resume";
        }
        cancel_all_waiters();
        return BlockingAsyncStatus::Blocked;
    }

  private:
    void wait_for_ready_or_adopted_release() {
        const auto no_pending_wait_locked = [&] {
            return (!ctx_ || ctx_->adopted_contexts.load(std::memory_order_acquire) == 0) && !core_.next_timer_deadline();
        };
        const auto ready_or_done_waiting_locked = [&] { return core_.has_ready() || no_pending_wait_locked(); };

        std::unique_lock<std::mutex>                         wake_lk(adopted_release_wake_->mtx);
        const auto                                           wake_generation = adopted_release_wake_->generation;
        std::optional<std::chrono::steady_clock::time_point> wake_deadline;
        if (ready_or_done_waiting_locked()) {
            return;
        }
        wake_deadline            = core_.next_timer_deadline();
        const auto wake_observed = [&] { return adopted_release_wake_->generation != wake_generation; };
        if (wake_deadline) {
            (void)adopted_release_wake_->cv.wait_until(wake_lk, *wake_deadline, wake_observed);
        } else {
            adopted_release_wake_->cv.wait(wake_lk, wake_observed);
        }
    }

    void cancel_all_waiters() { core_.cancel_all(); }

    std::shared_ptr<TestContextInfo>                     ctx_;
    std::shared_ptr<TestContextInfo::AdoptedReleaseWake> adopted_release_wake_;
    gentest::runner::AsyncSchedulerCore                  core_;
};

auto make_context(std::string_view label) -> std::shared_ptr<TestContextInfo> {
    auto ctx          = std::make_shared<TestContextInfo>();
    ctx->display_name = std::string(label);
    start_context(*ctx);
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

    request_context_stop(*ctx);
    wait_for_adopted_contexts(ctx);
    close_context_to_late_operations(*ctx);
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
