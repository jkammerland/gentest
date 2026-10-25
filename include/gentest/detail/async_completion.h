#pragma once

#include "gentest/detail/runtime_export.h"

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

class AsyncFrame;
class AsyncFrameScope;
GENTEST_RUNTIME_API auto current_async_frame() noexcept -> AsyncFrame *;
GENTEST_RUNTIME_API auto set_current_async_frame(AsyncFrame *frame) noexcept -> AsyncFrame *;
GENTEST_RUNTIME_API auto current_async_frame_scope() noexcept -> AsyncFrameScope *;
GENTEST_RUNTIME_API auto set_current_async_frame_scope(AsyncFrameScope *scope) noexcept -> AsyncFrameScope *;

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
    explicit AsyncSuspension(std::uint64_t value, const std::stop_token &inherited = {}) : generation(value) {
        if (inherited.stop_possible()) {
            parent_stop = std::make_unique<std::stop_callback<std::function<void()>>>(
                inherited, [source = stop]() mutable noexcept { source.request_stop(); });
        }
    }

    const std::uint64_t                                        generation;
    std::atomic_bool                                           active{true};
    std::atomic_bool                                           admitted{false};
    std::stop_source                                           stop;
    std::unique_ptr<std::stop_callback<std::function<void()>>> parent_stop;
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
    [[nodiscard]] auto done() const noexcept -> bool { return !handle || completed_; }
    void               mark_completed() noexcept { completed_ = true; }
    [[nodiscard]] auto is_canceled() const noexcept -> bool { return canceled_; }

    [[nodiscard]] auto try_claim_waiter() noexcept -> bool { return !std::exchange(waiter_claimed_, true); }

    // A coroutine binds its cancellation context once, before its first execution.
    void set_stop_token(const std::stop_token &token) {
        stop_bound_ = true;
        parent_stop_.reset();
        if (token.stop_possible()) {
            parent_stop_ = std::make_unique<std::stop_callback<std::function<void()>>>(
                token, [source = stop_]() mutable noexcept { source.request_stop(); });
        }
    }

    void inherit_stop_token(const std::stop_token &token) {
        if (!stop_bound_) {
            set_stop_token(token);
        }
    }

    [[nodiscard]] auto stop_token() const noexcept -> std::stop_token { return stop_.get_token(); }

    // Composition owns input awaiters outside their adapter coroutine. Pin that
    // storage during delivery and execution, without retaining it while idle.
    void set_execution_lifetime(std::weak_ptr<void> lifetime) noexcept { execution_lifetime_ = std::move(lifetime); }

    using CompletionObserver = std::function<void(AsyncResumeReason, AsyncTimePoint)>;

    // Timed composition installs this only at its actual input suspension.
    void observe_next_completion(CompletionObserver observer) { completion_observer_ = std::move(observer); }
    void notify_completion(AsyncResumeReason reason, AsyncTimePoint received) {
        if (auto observer = std::exchange(completion_observer_, {})) {
            observer(reason, received);
        }
    }

    // A failed await_suspend (or one returning false) need not pass through
    // resume_claimed(). Retire its capabilities when registering the next wait.
    void begin_wait() noexcept {
        auto previous = std::move(suspension_);
        if (previous) {
            previous->active.store(false, std::memory_order_release);
        }
    }

    // Called only on the runner. All competitors receive this same registration.
    [[nodiscard]] auto suspension() -> std::shared_ptr<AsyncSuspension> {
        if (!suspension_) {
            if (++generation_ == 0) {
                std::abort();
            }
            suspension_ = std::make_shared<AsyncSuspension>(generation_, stop_token());
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
        if (done()) {
            return;
        }
        stop_.request_stop();
        if (suspension_) {
            suspension_->stop.request_stop();
        }
    }

    void prepare_resume() {
        (void)set_current_async_frame(this);
        suspension_.reset();
    }

    void resume_claimed() {
        prepare_resume();
        handle.resume();
    }

    std::coroutine_handle<> handle{};

  private:
    friend class AsyncFrameScope;
    // Execution ownership is separate from cancellable scheduler registration.
    AsyncFrameScope                                           *execution_scope_    = nullptr;
    AsyncFrame                                                *execution_previous_ = nullptr;
    std::shared_ptr<AsyncFrame>                                execution_next_;
    std::weak_ptr<void>                                        execution_lifetime_;
    std::shared_ptr<void>                                      execution_lifetime_pin_;
    std::shared_ptr<AsyncSuspension>                           suspension_;
    std::uint64_t                                              generation_     = 0;
    bool                                                       canceled_       = false;
    bool                                                       completed_      = false;
    bool                                                       waiter_claimed_ = false;
    bool                                                       stop_bound_     = false;
    std::stop_source                                           stop_;
    std::unique_ptr<std::stop_callback<std::function<void()>>> parent_stop_;
    CompletionObserver                                         completion_observer_;
};

using AsyncFramePtr = std::shared_ptr<AsyncFrame>;

class AsyncFrameScope {
  public:
    explicit AsyncFrameScope(const AsyncFramePtr &frame) noexcept
        : previous_(set_current_async_frame(frame.get())), previous_scope_(set_current_async_frame_scope(this)) {
        retain(frame);
    }
    AsyncFrameScope(const AsyncFrameScope &)                     = delete;
    auto operator=(const AsyncFrameScope &) -> AsyncFrameScope & = delete;
    ~AsyncFrameScope() {
        // Destructors may reenter the runtime; restore its surrounding context
        // before releasing frames, and clear links before each destruction.
        (void)set_current_async_frame_scope(previous_scope_);
        (void)set_current_async_frame(previous_);
        while (retained_) {
            auto frame = std::move(retained_);
            retained_  = std::move(frame->execution_next_);
            if (retained_) {
                retained_->execution_previous_ = nullptr;
            }
            frame->execution_scope_    = nullptr;
            frame->execution_previous_ = nullptr;
            auto lifetime              = std::move(frame->execution_lifetime_pin_);
        }
    }

    void retain(const AsyncFramePtr &frame) noexcept {
        if (!frame || frame->execution_scope_) {
            return;
        }
        frame->execution_lifetime_pin_ = frame->execution_lifetime_.lock();
        frame->execution_scope_        = this;
        frame->execution_next_         = std::move(retained_);
        if (frame->execution_next_) {
            frame->execution_next_->execution_previous_ = frame.get();
        }
        retained_ = frame;
    }

    static void release_completed(const AsyncFramePtr &frame) noexcept {
        if (!frame || !frame->done() || frame.get() == current_async_frame() || !frame->execution_scope_) {
            return;
        }
        auto  retained = frame;
        auto *previous = frame->execution_previous_;
        auto &link     = previous ? previous->execution_next_ : frame->execution_scope_->retained_;
        link           = std::move(frame->execution_next_);
        if (link) {
            link->execution_previous_ = previous;
        }
        frame->execution_scope_    = nullptr;
        frame->execution_previous_ = nullptr;
        auto lifetime              = std::move(frame->execution_lifetime_pin_);
        lifetime.reset();
        retained.reset();
    }

  private:
    AsyncFrame      *previous_;
    AsyncFrameScope *previous_scope_;
    AsyncFramePtr    retained_;
};

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
        AsyncFrameScope scope(frame);
        frame->notify_completion(completion.reason, completion.admitted_at);
        if (frame->is_canceled() || frame->done()) {
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
