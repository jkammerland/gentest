#include "gentest/async.h"

#include "gentest/detail/runtime_context.h"
#include "gentest/detail/runtime_support.h"

#include <deque>
#include <fmt/format.h>
#include <unordered_map>

namespace gentest::detail {
namespace {

thread_local AsyncScheduler *g_current_async_scheduler = nullptr;

class BlockingAsyncScheduler final : public AsyncScheduler {
  public:
    explicit BlockingAsyncScheduler(std::shared_ptr<TestContextInfo> ctx) : ctx_(std::move(ctx)) {}

    void post(std::coroutine_handle<> handle) override {
        if (!handle || handle.done()) {
            return;
        }
        blocked_.erase(handle.address());
        ready_.push_back(handle);
    }

    void block(std::coroutine_handle<> handle, std::string reason) override {
        if (!handle) {
            return;
        }
        blocked_[handle.address()] = reason.empty() ? std::string("async task cannot resume") : std::move(reason);
    }

    void attach_child(std::coroutine_handle<> child, std::coroutine_handle<> parent) override {
        if (!child || !parent) {
            return;
        }
        const auto parent_it = owners_.find(parent.address());
        if (parent_it != owners_.end()) {
            owners_[child.address()] = parent_it->second;
        }
    }

    [[nodiscard]] auto run(AsyncTask &task, std::string &blocked_reason) -> bool {
        AsyncSchedulerScope scheduler_scope(this);
        task.set_scheduler(this);
        owners_[task.handle().address()] = 0;
        post(task.handle());

        while (!ready_.empty()) {
            auto handle = ready_.front();
            ready_.pop_front();
            if (!handle || handle.done()) {
                continue;
            }
            auto previous = current_test();
            set_current_test(ctx_);
            try {
                handle.resume();
            } catch (const std::exception &e) {
                blocked_reason = fmt::format("async coroutine resume threw std::exception: {}", e.what());
                set_current_test(std::move(previous));
                return false;
            } catch (...) {
                blocked_reason = "async coroutine resume threw unknown exception";
                set_current_test(std::move(previous));
                return false;
            }
            set_current_test(std::move(previous));
        }

        if (!task.handle() || task.handle().done()) {
            return true;
        }

        if (!blocked_.empty()) {
            blocked_reason = blocked_.begin()->second;
        } else {
            blocked_reason = "async task cannot resume";
        }
        return false;
    }

  private:
    std::shared_ptr<TestContextInfo>        ctx_;
    std::deque<std::coroutine_handle<>>     ready_;
    std::unordered_map<void *, std::size_t> owners_;
    std::unordered_map<void *, std::string> blocked_;
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
    const bool             completed = task_ptr && scheduler.run(*task_ptr, blocked_reason);

    wait_for_adopted_contexts(ctx);
    ctx->active = false;
    flush_current_buffer_for(ctx.get());

    if (!completed) {
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
