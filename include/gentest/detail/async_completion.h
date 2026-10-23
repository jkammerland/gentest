#pragma once

// Native coroutine lifetime and generation guards. The capability protocol is
// adapted from the author's coroutine core under Gentest's BSL-1.0 license.

#include <atomic>
#include <chrono>
#include <concepts>
#include <coroutine>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <stop_token>
#include <type_traits>
#include <utility>

namespace gentest::detail {

// Only the void callback signature needed by asynchronous operations is erased.
template <typename... Arguments> class AsyncCallback {
    struct Callable {
        virtual ~Callable()                         = default;
        virtual void invoke(Arguments... arguments) = 0;
    };

    template <typename Function> struct Model final : Callable {
        explicit Model(Function function) : function_(std::move(function)) {}

        void invoke(Arguments... arguments) override { std::invoke(function_, std::forward<Arguments>(arguments)...); }

        Function function_;
    };

  public:
    AsyncCallback() noexcept = default;
    AsyncCallback(std::nullptr_t) noexcept {}
    AsyncCallback(AsyncCallback &&) noexcept                     = default;
    auto operator=(AsyncCallback &&) noexcept -> AsyncCallback & = default;

    template <typename Function>
        requires(!std::same_as<std::remove_cvref_t<Function>, AsyncCallback> &&
                 std::is_invocable_r_v<void, std::decay_t<Function> &, Arguments...>)
    AsyncCallback(Function &&function) : callable_(std::make_unique<Model<std::decay_t<Function>>>(std::forward<Function>(function))) {}

    [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(callable_); }

    void operator()(Arguments... arguments) const {
        if (!callable_) {
            std::abort();
        }
        callable_->invoke(std::forward<Arguments>(arguments)...);
    }

  private:
    std::unique_ptr<Callable> callable_;
};

enum class AsyncResumeReason { Ready, Timeout };
using AsyncTimePoint = std::chrono::steady_clock::time_point;

// Admission flags cross the inbox boundary; frame and generation access do not.
struct AsyncSuspension {
    explicit AsyncSuspension(std::uint64_t value) : generation(value) {}

    const std::uint64_t generation;
    std::atomic_bool    active{true};
    std::atomic_bool    admitted{false};
    std::stop_source    stop;
};

class AsyncFrame {
  public:
    explicit AsyncFrame(std::coroutine_handle<> coroutine) noexcept : handle(coroutine) {}
    AsyncFrame(const AsyncFrame &)                     = delete;
    auto operator=(const AsyncFrame &) -> AsyncFrame & = delete;

    ~AsyncFrame() {
        cancel();
        if (handle) {
            handle.destroy();
        }
    }

    [[nodiscard]] auto address() const noexcept -> void * { return handle.address(); }
    [[nodiscard]] auto done() const noexcept -> bool { return !handle || handle.done(); }
    [[nodiscard]] auto is_canceled() const noexcept -> bool { return canceled_; }

    [[nodiscard]] auto try_claim_waiter() noexcept -> bool { return !std::exchange(waiter_claimed_, true); }

    // Called only on the runner. All competitors receive this same registration.
    [[nodiscard]] auto suspension() -> std::shared_ptr<AsyncSuspension> {
        if (!suspension_) {
            if (++generation_ == 0) {
                std::abort();
            }
            suspension_ = std::make_shared<AsyncSuspension>(generation_);
        }
        return suspension_;
    }

    [[nodiscard]] auto matches(const std::shared_ptr<AsyncSuspension> &registration) const noexcept -> bool {
        return registration && suspension_ == registration && registration->generation == generation_ && !canceled_ && !done();
    }

    [[nodiscard]] auto claim(const std::shared_ptr<AsyncSuspension> &registration) noexcept -> bool {
        return matches(registration) && registration->active.exchange(false, std::memory_order_acq_rel);
    }

    // Invalidate under the scheduler lock; request stop only after releasing it.
    void invalidate() noexcept {
        canceled_ = true;
        if (suspension_) {
            suspension_->active.store(false, std::memory_order_release);
        }
    }

    void cancel() noexcept {
        invalidate();
        if (suspension_) {
            suspension_->stop.request_stop();
        }
    }

    void resume_claimed() {
        suspension_.reset();
        handle.resume();
    }

    std::coroutine_handle<> handle{};

  private:
    std::shared_ptr<AsyncSuspension> suspension_;
    std::uint64_t                    generation_     = 0;
    bool                             canceled_       = false;
    bool                             waiter_claimed_ = false;
};

using AsyncFramePtr = std::shared_ptr<AsyncFrame>;

struct AsyncCompletion {
    std::weak_ptr<AsyncFrame>                                           frame;
    std::shared_ptr<AsyncSuspension>                                    suspension;
    std::shared_ptr<std::atomic_bool>                                   canceled;
    void                                                               *address = nullptr;
    AsyncResumeReason                                                   reason  = AsyncResumeReason::Ready;
    AsyncTimePoint                                                      admitted_at{};
    std::function<AsyncResumeReason(AsyncResumeReason, AsyncTimePoint)> select;
    AsyncCallback<>                                                     publish;

    [[nodiscard]] auto active() const noexcept -> bool {
        return suspension && suspension->active.load(std::memory_order_acquire) &&
               (!canceled || !canceled->load(std::memory_order_acquire));
    }
};

struct AsyncReadyEntry {
    AsyncFramePtr   frame;
    AsyncCompletion completion;

    [[nodiscard]] explicit operator bool() const noexcept { return frame != nullptr; }

    // Called with the owning test context restored, outside every inbox lock.
    [[nodiscard]] auto resume() -> bool {
        if (!frame || !completion.active() || !frame->claim(completion.suspension)) {
            return false;
        }
        auto reason = completion.select ? completion.select(completion.reason, completion.admitted_at) : completion.reason;
        if (reason == AsyncResumeReason::Ready && completion.publish) {
            completion.publish();
        }
        if (frame->is_canceled() || frame->done()) {
            return false;
        }
        frame->resume_claimed();
        return true;
    }
};

} // namespace gentest::detail
