#pragma once

#include "gentest/detail/async_api.h"
#include "gentest/detail/async_completion.h"
#include "gentest/detail/runtime_base.h"
#include "gentest/framework_events.h"

#include <any>
#include <atomic>
#include <chrono>
#include <concepts>
#include <condition_variable>
#include <coroutine>
#include <cstdlib>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace gentest {

template <typename T = void> class awaitable;

namespace detail {

class AsyncScheduler {
  public:
    using ResumeReason = AsyncResumeReason;
    class Control {
      public:
        void               post(std::coroutine_handle<> handle) const;
        void               post(const AsyncFramePtr &frame) const;
        [[nodiscard]] auto post(AsyncCompletion completion) const -> bool;
        void               notify() const noexcept;
        void               cancel_waiters(std::coroutine_handle<> handle) const noexcept;

      private:
        friend class AsyncScheduler;

        const std::thread::id owner_thread_ = std::this_thread::get_id();
        mutable std::mutex    mtx_;
        AsyncScheduler       *scheduler_ = nullptr;
    };

    class WaiterToken {
      public:
        using Completion = std::function<ResumeReason(ResumeReason, AsyncTimePoint)>;

        WaiterToken(std::weak_ptr<Control> control, const AsyncFramePtr &frame)
            : control_(std::move(control)), frame_(frame), suspension_(frame ? frame->suspension() : nullptr),
              address_(frame ? frame->address() : nullptr) {}

        void post() {
            (void)try_invoke([]() noexcept {});
        }

        template <typename Publish>
            requires std::is_nothrow_invocable_v<std::decay_t<Publish> &>
        [[nodiscard]] auto try_invoke(Publish &&publish) const noexcept -> bool {
            auto control = control_.lock();
            if (!control || !active()) {
                return false;
            }
            return control->post(AsyncCompletion{.frame      = frame_,
                                                 .suspension = suspension_,
                                                 .canceled   = canceled_,
                                                 .address    = address_,
                                                 .reason     = reason_,
                                                 .select     = completion_,
                                                 .publish    = AsyncCallback<>{std::forward<Publish>(publish)}});
        }

        void cancel() noexcept {
            if (!canceled_->exchange(true, std::memory_order_acq_rel) && external()) {
                notify_external_change();
            }
        }
        void retain_external() noexcept { external_.fetch_add(1, std::memory_order_relaxed); }
        void release_external() noexcept {
            if (external_.fetch_sub(1, std::memory_order_acq_rel) == 1 && active()) {
                notify_external_change();
            }
        }
        [[nodiscard]] auto external() const noexcept -> bool { return external_.load(std::memory_order_acquire) != 0; }
        void               request_stop() noexcept {
            if (suspension_) {
                suspension_->stop.request_stop();
            }
        }
        [[nodiscard]] auto stop_token() const noexcept -> std::stop_token {
            return suspension_ ? suspension_->stop.get_token() : std::stop_token{};
        }
        void set_completion(Completion completion, ResumeReason reason) {
            completion_ = std::move(completion);
            reason_     = reason;
        }
        [[nodiscard]] auto active() const noexcept -> bool {
            return suspension_ && suspension_->active.load(std::memory_order_acquire) &&
                   (reason_ == ResumeReason::Timeout || !suspension_->admitted.load(std::memory_order_acquire)) &&
                   !canceled_->load(std::memory_order_acquire);
        }

      private:
        void notify_external_change() const noexcept {
            if (auto control = control_.lock()) {
                control->notify();
            }
        }
        std::weak_ptr<Control>            control_;
        std::weak_ptr<AsyncFrame>         frame_;
        std::shared_ptr<AsyncSuspension>  suspension_;
        std::shared_ptr<std::atomic_bool> canceled_ = std::make_shared<std::atomic_bool>(false);
        void                             *address_  = nullptr;
        Completion                        completion_;
        std::atomic<std::uint32_t>        external_{0};
        ResumeReason                      reason_ = ResumeReason::Ready;
    };

    using WaiterTokenPtr = std::shared_ptr<WaiterToken>;

    AsyncScheduler() : control_(std::make_shared<Control>()) { control_->scheduler_ = this; }
    virtual ~AsyncScheduler() { deactivate(); }

    virtual void notify_completion() noexcept {}

    [[nodiscard]] virtual auto frame_for(std::coroutine_handle<>) const -> AsyncFramePtr { return {}; }
    virtual void               prepare_transfer(const AsyncFramePtr &frame) {
        if (auto *scope = current_async_frame_scope()) {
            scope->retain(frame);
        }
        frame->prepare_resume();
    }

    // Call only at public suspension boundaries, before acquiring scheduler or
    // awaiter locks. Raw begin_wait also rearms waits and must stay unobserved.
    void suspension_point(std::coroutine_handle<> handle, SuspensionKind kind, const std::source_location &location = {}) noexcept {
        if (const auto frame = frame_for(handle)) {
            framework_suspend(frame->framework_context.lock(), kind, location);
        }
    }

    virtual void               begin_wait(std::coroutine_handle<> handle) noexcept   = 0;
    virtual void               post(std::coroutine_handle<> handle)                  = 0;
    [[nodiscard]] virtual auto post_completion(AsyncCompletion &&completion) -> bool = 0;
    virtual void               post_frame(const AsyncFramePtr &frame) {
        if (frame && frame->handle) {
            post(frame->handle);
        }
    }
    virtual void block(std::coroutine_handle<> handle, std::string reason)                   = 0;
    virtual void attach_child(std::coroutine_handle<> child, std::coroutine_handle<> parent) = 0;
    virtual void attach_child_frame(const AsyncFramePtr &child, std::coroutine_handle<> parent) {
        if (child && child->handle) {
            attach_child(child->handle, parent);
        }
    }

    virtual void block_at(std::coroutine_handle<> handle, std::string reason, const std::source_location &) {
        block(handle, std::move(reason));
    }

    virtual void yield_at(std::coroutine_handle<> handle, const std::source_location &) { post(handle); }

    virtual void schedule_timer(std::chrono::steady_clock::time_point deadline, const WaiterTokenPtr &token) {
        if (deadline <= std::chrono::steady_clock::now() && token) {
            token->post();
            return;
        }
        std::abort();
    }

    virtual void cancel_waiters(std::coroutine_handle<> handle) noexcept { (void)handle; }

    [[nodiscard]] auto control() const noexcept -> std::shared_ptr<Control> { return control_; }

    [[nodiscard]] virtual auto make_waiter(std::coroutine_handle<> handle) -> WaiterTokenPtr {
        (void)handle;
        auto token = std::make_shared<WaiterToken>(control_, AsyncFramePtr{});
        token->cancel();
        return token;
    }

  protected:
    void deactivate() noexcept {
        if (!control_) {
            return;
        }
        std::lock_guard<std::mutex> lk(control_->mtx_);
        control_->scheduler_ = nullptr;
    }

  private:
    std::shared_ptr<Control> control_;
};

inline void AsyncScheduler::Control::post(std::coroutine_handle<> handle) const {
    std::lock_guard<std::mutex> lk(mtx_);
    if (scheduler_) {
        scheduler_->post(handle);
    }
}

inline void AsyncScheduler::Control::post(const AsyncFramePtr &frame) const {
    std::lock_guard<std::mutex> lk(mtx_);
    if (scheduler_) {
        scheduler_->post_frame(frame);
    }
}

inline void AsyncScheduler::Control::cancel_waiters(std::coroutine_handle<> handle) const noexcept {
    AsyncScheduler *scheduler;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        scheduler = scheduler_;
    }
    // Cancellation is runner-only and may synchronously request backend stop.
    if (scheduler) {
        scheduler->cancel_waiters(handle);
    }
}

inline auto AsyncScheduler::Control::post(AsyncCompletion completion) const -> bool {
    // Keep rejected payload ownership here, so its destructor runs after unlock.
    std::lock_guard<std::mutex> lock(mtx_);
    return scheduler_ && completion.active() && scheduler_->post_completion(std::move(completion));
}

inline void AsyncScheduler::Control::notify() const noexcept {
    if (owner_thread_ == std::this_thread::get_id()) {
        return;
    }
    std::lock_guard<std::mutex> lock(mtx_);
    if (scheduler_) {
        scheduler_->notify_completion();
    }
}

GENTEST_RUNTIME_API auto current_async_scheduler() noexcept -> AsyncScheduler *;
GENTEST_RUNTIME_API auto set_current_async_scheduler(AsyncScheduler *scheduler) noexcept -> AsyncScheduler *;

class AsyncSchedulerScope {
  public:
    explicit AsyncSchedulerScope(AsyncScheduler *scheduler) noexcept : previous_(set_current_async_scheduler(scheduler)) {}
    AsyncSchedulerScope(const AsyncSchedulerScope &)            = delete;
    AsyncSchedulerScope &operator=(const AsyncSchedulerScope &) = delete;
    ~AsyncSchedulerScope() { (void)set_current_async_scheduler(previous_); }

  private:
    AsyncScheduler *previous_ = nullptr;
};

class blocked_exception : public std::runtime_error {
  public:
    explicit blocked_exception(std::string reason) : std::runtime_error(reason), reason_(std::move(reason)) {}
    [[nodiscard]] auto reason() const -> const std::string & { return reason_; }

  private:
    std::string reason_;
};

class AsyncTask {
  public:
    virtual ~AsyncTask() = default;

    [[nodiscard]] virtual auto handle() const noexcept -> std::coroutine_handle<> = 0;
    [[nodiscard]] virtual auto frame() const noexcept -> AsyncFramePtr            = 0;
    virtual void               set_scheduler(AsyncScheduler *scheduler) noexcept  = 0;
    [[nodiscard]] virtual auto exception() const noexcept -> std::exception_ptr   = 0;
};

template <typename T>
concept EventDefaultSetPayload = std::default_initializable<T> && std::assignable_from<T &, T &&>;

template <typename T, typename U>
concept EventSetPayload = std::constructible_from<T, U &&> && std::assignable_from<T &, T &&>;

} // namespace detail

} // namespace gentest

#include "gentest/detail/async_continuation.h"
#include "gentest/detail/async_stop.h"

namespace gentest::async {

template <typename T> class promise;

enum class wait_status { ready, timeout };

namespace wait_result_detail {

[[noreturn]] inline void wait_result_timeout_value() {
#if GENTEST_EXCEPTIONS_ENABLED
    throw std::logic_error("gentest::async::wait_result has no value because the wait timed out");
#else
    std::abort();
#endif
}

} // namespace wait_result_detail

template <typename T> class wait_result {
    struct empty_value {};
    using storage_type = std::variant<empty_value, T>;

  public:
    [[nodiscard]] static auto make_ready(T value) -> wait_result {
        return wait_result(wait_status::ready, storage_type{std::in_place_index<1>, std::move(value)});
    }
    [[nodiscard]] static auto make_timeout() -> wait_result {
        return wait_result(wait_status::timeout, storage_type{std::in_place_index<0>});
    }

    [[nodiscard]] auto     ready() const noexcept -> bool { return status_ == wait_status::ready; }
    [[nodiscard]] auto     timed_out() const noexcept -> bool { return status_ == wait_status::timeout; }
    [[nodiscard]] auto     status() const noexcept -> wait_status { return status_; }
    [[nodiscard]] explicit operator bool() const noexcept { return ready(); }

    [[nodiscard]] auto value() & -> T & {
        ensure_value();
        return std::get<1>(value_);
    }

    [[nodiscard]] auto value() const & -> const T & {
        ensure_value();
        return std::get<1>(value_);
    }

    [[nodiscard]] auto value() && -> T {
        ensure_value();
        return std::get<1>(std::move(value_));
    }

  private:
    explicit wait_result(wait_status status, storage_type value) : status_(status), value_(std::move(value)) {}

    void ensure_value() const {
        if (!std::holds_alternative<T>(value_)) {
            wait_result_detail::wait_result_timeout_value();
        }
    }

    wait_status  status_ = wait_status::timeout;
    storage_type value_;
};

template <typename T> class wait_result<T &> {
  public:
    [[nodiscard]] static auto make_ready(T &value) -> wait_result { return wait_result(wait_status::ready, &value); }
    [[nodiscard]] static auto make_timeout() -> wait_result { return wait_result(wait_status::timeout, nullptr); }

    [[nodiscard]] auto     ready() const noexcept -> bool { return status_ == wait_status::ready; }
    [[nodiscard]] auto     timed_out() const noexcept -> bool { return status_ == wait_status::timeout; }
    [[nodiscard]] auto     status() const noexcept -> wait_status { return status_; }
    [[nodiscard]] explicit operator bool() const noexcept { return ready(); }

    [[nodiscard]] auto value() const -> T & {
        if (!value_) {
            wait_result_detail::wait_result_timeout_value();
        }
        return *value_;
    }

  private:
    constexpr wait_result(wait_status status, T *value) noexcept : status_(status), value_(value) {}

    wait_status status_ = wait_status::timeout;
    T          *value_  = nullptr;
};

template <> class wait_result<void> {
  public:
    [[nodiscard]] static constexpr auto make_ready() noexcept -> wait_result { return wait_result(wait_status::ready); }
    [[nodiscard]] static constexpr auto make_timeout() noexcept -> wait_result { return wait_result(wait_status::timeout); }

    [[nodiscard]] constexpr auto     ready() const noexcept -> bool { return status_ == wait_status::ready; }
    [[nodiscard]] constexpr auto     timed_out() const noexcept -> bool { return status_ == wait_status::timeout; }
    [[nodiscard]] constexpr auto     status() const noexcept -> wait_status { return status_; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return ready(); }

  private:
    explicit constexpr wait_result(wait_status status) noexcept : status_(status) {}

    wait_status status_ = wait_status::timeout;
};

class yield_awaitable {
  public:
    explicit yield_awaitable(std::source_location loc, std::optional<std::stop_token> stop = {})
        : loc_(loc), cancellation_(std::move(stop)) {}

    [[nodiscard]] constexpr auto await_ready() const noexcept -> bool { return false; }

    void await_suspend(std::coroutine_handle<> handle) {
        auto *scheduler = detail::current_async_scheduler();
        if (!scheduler) {
            std::abort();
        }
        scheduler->suspension_point(handle, SuspensionKind::yield, loc_);
        scheduler->begin_wait(handle);
        cancellation_.arm(scheduler->make_waiter(handle));
        scheduler->yield_at(handle, loc_);
    }

    void await_resume() const { cancellation_.check(); }

  private:
    std::source_location          loc_;
    detail::AsyncWaitCancellation cancellation_;
};

[[nodiscard]] inline auto yield(const std::source_location &loc = std::source_location::current()) -> yield_awaitable {
    return yield_awaitable{loc};
}

[[nodiscard]] inline auto yield(std::stop_token stop, const std::source_location &loc = std::source_location::current())
    -> yield_awaitable {
    return yield_awaitable{loc, stop};
}

class sleep_awaitable {
  public:
    sleep_awaitable(std::chrono::steady_clock::time_point deadline, std::source_location loc, std::optional<std::stop_token> stop = {})
        : deadline_(deadline), loc_(loc), cancellation_(std::move(stop)) {}
    sleep_awaitable(sleep_awaitable &&) noexcept = default;

    [[nodiscard]] auto await_ready() const -> bool {
        cancellation_.check();
        return std::chrono::steady_clock::now() >= deadline_;
    }

    void await_suspend(std::coroutine_handle<> handle) {
        auto *scheduler = detail::current_async_scheduler();
        if (!scheduler) {
            std::abort();
        }
        scheduler->suspension_point(handle, SuspensionKind::timer, loc_);
        scheduler->begin_wait(handle);
        token_ = scheduler->make_waiter(handle);
        cancellation_.arm(token_);
        scheduler->block_at(handle, "async timer has not elapsed", loc_);
        scheduler->schedule_timer(deadline_, token_);
    }

    void await_resume() const { cancellation_.check(); }

    ~sleep_awaitable() {
        if (token_) {
            token_->cancel();
        }
    }

  private:
    std::chrono::steady_clock::time_point  deadline_;
    std::source_location                   loc_;
    detail::AsyncScheduler::WaiterTokenPtr token_;
    detail::AsyncWaitCancellation          cancellation_;
};

template <typename Rep, typename Period>
[[nodiscard]] inline auto sleep_for(std::chrono::duration<Rep, Period> duration,
                                    const std::source_location        &loc = std::source_location::current()) -> sleep_awaitable {
    return sleep_awaitable{std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(duration),
                           loc};
}

template <typename Rep, typename Period>
[[nodiscard]] inline auto sleep_for(std::chrono::duration<Rep, Period> duration, std::stop_token stop,
                                    const std::source_location &loc = std::source_location::current()) -> sleep_awaitable {
    return sleep_awaitable{std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(duration),
                           loc, stop};
}

template <typename Duration>
[[nodiscard]] inline auto sleep_until(std::chrono::time_point<std::chrono::steady_clock, Duration> deadline,
                                      const std::source_location &loc = std::source_location::current()) -> sleep_awaitable {
    return sleep_awaitable{std::chrono::time_point_cast<std::chrono::steady_clock::duration>(deadline), loc};
}

template <typename Duration>
[[nodiscard]] inline auto sleep_until(std::chrono::time_point<std::chrono::steady_clock, Duration> deadline, std::stop_token stop,
                                      const std::source_location &loc = std::source_location::current()) -> sleep_awaitable {
    return sleep_awaitable{std::chrono::time_point_cast<std::chrono::steady_clock::duration>(deadline), loc, stop};
}

template <typename T = std::any> class event {
    struct Slot;

    struct WaitState {
        T *value = nullptr;
    };

    struct Waiter {
        detail::AsyncScheduler::WaiterTokenPtr token;
        std::weak_ptr<WaitState>               state;
    };

    struct Slot {
        bool                ready = false;
        std::optional<T>    value;
        std::vector<Waiter> waiters;
    };

  public:
    event() = default;

    void set(std::string key)
        requires detail::EventDefaultSetPayload<T>
    {
        set_default(std::move(key));
    }

    void set(std::string key, std::type_identity_t<T> value)
        requires detail::EventSetPayload<T, T>
    {
        set_value(std::move(key), std::move(value));
    }

    template <typename U>
        requires detail::EventSetPayload<T, U>
    void set(std::string key, U &&value) {
        set_value(std::move(key), std::forward<U>(value));
    }

    void reset(std::string_view key) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto                        it = slots_.find(std::string(key));
        if (it == slots_.end()) {
            return;
        }
        it->second.ready = false;
    }

    void reset_all() {
        std::lock_guard<std::mutex> lk(mtx_);
        for (auto &entry : slots_) {
            auto &slot = entry.second;
            slot.ready = false;
        }
    }

    [[nodiscard]] auto is_set(std::string_view key) const -> bool {
        std::lock_guard<std::mutex> lk(mtx_);
        const auto                  it = slots_.find(std::string(key));
        return it != slots_.end() && it->second.ready;
    }

    class awaitable {
      public:
        awaitable(event &owner, std::string key, std::source_location loc, std::optional<std::stop_token> stop = {})
            : event_(owner), key_(std::move(key)), loc_(loc), state_(std::make_shared<WaitState>()), cancellation_(std::move(stop)) {}

        awaitable(awaitable &&other) noexcept
            : event_(other.event_), key_(std::move(other.key_)), loc_(other.loc_), state_(std::move(other.state_)),
              registered_(std::exchange(other.registered_, false)), cancellation_(std::move(other.cancellation_)) {}

        awaitable &operator=(awaitable &&) noexcept = delete;
        awaitable(const awaitable &)                = delete;
        awaitable &operator=(const awaitable &)     = delete;

        ~awaitable() { unregister(); }

        [[nodiscard]] auto await_ready() -> bool {
            cancellation_.check();
            std::lock_guard<std::mutex> lk(event_.mtx_);
            auto                        it = event_.slots_.find(key_);
            if (it == event_.slots_.end() || !it->second.ready) {
                return false;
            }
            state_->value = value_ptr(it->second);
            return true;
        }

        void await_suspend(std::coroutine_handle<> handle) {
            auto *scheduler = detail::current_async_scheduler();
            if (!scheduler) {
                std::abort();
            }
            scheduler->suspension_point(handle, SuspensionKind::event, loc_);
            scheduler->begin_wait(handle);
            auto token       = scheduler->make_waiter(handle);
            bool should_post = false;
            {
                std::lock_guard<std::mutex> lk(event_.mtx_);
                auto                       &slot = event_.slots_[key_];
                if (slot.ready) {
                    state_->value = value_ptr(slot);
                    should_post   = true;
                } else {
                    slot.waiters.push_back(Waiter{.token = token, .state = state_});
                    registered_ = true;
                    scheduler->block_at(handle, event_.blocked_reason(key_), loc_);
                }
            }
            cancellation_.arm(token);
            if (should_post) {
                token->post();
            }
        }

        auto await_resume() -> T & {
            unregister();
            cancellation_.check();
            if (!state_ || state_->value == nullptr) {
#if GENTEST_EXCEPTIONS_ENABLED
                throw std::runtime_error("gentest::async::event resumed without payload slot");
#else
                std::abort();
#endif
            }
            return *state_->value;
        }

      private:
        void unregister() noexcept {
            if (!registered_ || !state_) {
                return;
            }
            std::lock_guard<std::mutex> lk(event_.mtx_);
            auto                        it = event_.slots_.find(key_);
            if (it == event_.slots_.end()) {
                registered_ = false;
                return;
            }
            auto &waiters = it->second.waiters;
            std::erase_if(waiters, [&](const Waiter &waiter) {
                auto state = waiter.state.lock();
                return !state || state == state_;
            });
            registered_ = false;
        }

        event                        &event_;
        std::string                   key_;
        std::source_location          loc_;
        std::shared_ptr<WaitState>    state_;
        bool                          registered_ = false;
        detail::AsyncWaitCancellation cancellation_;
    };

    [[nodiscard]] auto wait(std::string key, const std::source_location &loc = std::source_location::current()) -> awaitable {
        return awaitable{*this, std::move(key), loc};
    }

    [[nodiscard]] auto wait(std::string key, std::stop_token stop, const std::source_location &loc = std::source_location::current())
        -> awaitable {
        return awaitable{*this, std::move(key), loc, stop};
    }

  private:
    void set_default(std::string key) {
        std::vector<Waiter> waiters;
        T                  *payload = nullptr;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            auto                       &slot = slots_[std::move(key)];
            if (slot.value.has_value()) {
                *slot.value = T{};
            } else {
                slot.value.emplace();
            }
            slot.ready = true;
            payload    = value_ptr(slot);
            waiters.swap(slot.waiters);
        }
        post_waiters(waiters, payload);
    }

    template <typename U> void set_value(std::string key, U &&value) {
        std::vector<Waiter> waiters;
        T                  *payload = nullptr;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            auto                       &slot = slots_[std::move(key)];
            if (slot.value.has_value()) {
                *slot.value = T(std::forward<U>(value));
            } else {
                slot.value.emplace(std::forward<U>(value));
            }
            slot.ready = true;
            payload    = value_ptr(slot);
            waiters.swap(slot.waiters);
        }
        post_waiters(waiters, payload);
    }

    void post_waiters(std::vector<Waiter> &waiters, T *payload) {
        for (auto &waiter : waiters) {
            if (auto state = waiter.state.lock()) {
                state->value = payload;
                if (waiter.token) {
                    waiter.token->post();
                }
            } else if (waiter.token) {
                waiter.token->cancel();
            }
        }
    }

    [[nodiscard]] static auto blocked_reason(std::string_view key) -> std::string {
        if (key.empty()) {
            return "async event key was not set";
        }
        std::string reason = "async event key '";
        reason += key;
        reason += "' was not set";
        return reason;
    }

    [[nodiscard]] static auto value_ptr(Slot &slot) noexcept -> T * {
        if (!slot.value.has_value()) {
            return nullptr;
        }
        return std::addressof(*slot.value);
    }

    mutable std::mutex                    mtx_;
    std::unordered_map<std::string, Slot> slots_;
};

using manual_event = event<std::any>;

} // namespace gentest::async

#include "gentest/detail/async_promise.h"

namespace gentest {

namespace detail {

struct final_resume_awaiter {
    [[nodiscard]] constexpr auto await_ready() const noexcept -> bool { return false; }

    template <typename Promise> auto await_suspend(std::coroutine_handle<Promise> handle) const noexcept -> std::coroutine_handle<> {
        auto &promise = handle.promise();
        promise.frame->mark_completed();

        // Group completion posts its parent. The scheduler retains this frame
        // until the owning case finishes, including throughout final suspension.
        if (auto complete = std::move(promise.on_complete)) {
            complete();
        }

        auto parent = promise.continuation.lock();
        if (!parent || parent->is_canceled() || parent->done()) {
            return std::noop_coroutine();
        }
        parent->notify_completion(AsyncResumeReason::Ready, std::chrono::steady_clock::now());
        if (parent->is_canceled()) {
            return std::noop_coroutine();
        }
        promise.scheduler->prepare_transfer(parent);
        return parent->handle;
    }

    constexpr void await_resume() const noexcept {}
};

template <typename T> struct AsyncTaskResult {
    std::optional<T>           value;
    template <typename U> void return_value(U &&result) { value.emplace(std::forward<U>(result)); }
};

template <> struct AsyncTaskResult<void> {
    constexpr void return_void() const noexcept {}
};

[[noreturn]] inline void async_task_error(const char *message) {
#if GENTEST_EXCEPTIONS_ENABLED
    throw std::logic_error(message);
#else
    (void)message;
    std::abort();
#endif
}

} // namespace detail

template <typename T> class awaitable final : public detail::AsyncTask {
  public:
    struct promise_type : detail::AsyncTaskResult<T> {
        detail::AsyncFrame               *frame     = nullptr;
        detail::AsyncScheduler           *scheduler = nullptr;
        std::weak_ptr<detail::AsyncFrame> continuation;
        detail::AsyncCallback<>           on_complete;
        std::exception_ptr                exception;

        [[nodiscard]] auto make_continuation(std::source_location loc = std::source_location::current()) -> async::continuation_handle {
            return async::make_continuation(std::coroutine_handle<promise_type>::from_promise(*this), "custom asynchronous operation", loc);
        }

        [[nodiscard]] auto get_return_object() -> awaitable { return awaitable{std::coroutine_handle<promise_type>::from_promise(*this)}; }

        [[nodiscard]] constexpr auto initial_suspend() const noexcept -> std::suspend_always { return {}; }
        [[nodiscard]] constexpr auto final_suspend() const noexcept -> detail::final_resume_awaiter { return {}; }
        void                         unhandled_exception() noexcept { exception = std::current_exception(); }
    };

    awaitable() = default;
    explicit awaitable(std::coroutine_handle<promise_type> handle) : frame_(std::make_shared<detail::AsyncFrame>(handle)) {
        handle.promise().frame = frame_.get();
    }
    awaitable(awaitable &&other) noexcept : frame_(std::move(other.frame_)) {}
    auto operator=(awaitable &&other) noexcept -> awaitable & {
        if (this != &other) {
            reset();
            frame_ = std::move(other.frame_);
        }
        return *this;
    }

    awaitable(const awaitable &)            = delete;
    awaitable &operator=(const awaitable &) = delete;
    ~awaitable() override { reset(); }

    [[nodiscard]] auto handle() const noexcept -> std::coroutine_handle<> override {
        return frame_ ? frame_->handle : std::coroutine_handle<>{};
    }
    [[nodiscard]] auto frame() const noexcept -> detail::AsyncFramePtr override { return frame_; }

    void set_scheduler(detail::AsyncScheduler *scheduler) noexcept override {
        if (auto handle = typed_handle()) {
            handle.promise().scheduler = scheduler;
        }
    }
    [[nodiscard]] auto exception() const noexcept -> std::exception_ptr override {
        return frame_ ? typed_handle().promise().exception : std::exception_ptr{};
    }

    [[nodiscard]] auto await_ready() const noexcept -> bool { return !frame_ || frame_->done(); }

    auto await_suspend(std::coroutine_handle<> parent) -> std::coroutine_handle<> {
        auto *scheduler = detail::current_async_scheduler();
        if (!scheduler || !frame_ || !frame_->handle) {
            std::abort();
        }
        if (!frame_->try_claim_waiter()) {
            detail::async_task_error("gentest::awaitable already has an active waiter (concurrent co_await is not supported)");
        }

        // A native call has exactly one continuation. External callbacks keep
        // their generation-guarded queue path; they cannot compete with this link.
        auto parent_frame = scheduler->frame_for(parent);
        if (!parent_frame) {
            detail::async_task_error("gentest::awaitable awaited outside its owning Gentest case");
        }
        scheduler->suspension_point(parent, SuspensionKind::native_child, {});
        scheduler->begin_wait(parent);
        scheduler->attach_child_frame(frame_, parent);
        auto &promise        = typed_handle().promise();
        promise.scheduler    = scheduler;
        promise.continuation = parent_frame;
        scheduler->prepare_transfer(frame_);
        return frame_->handle;
    }

    auto await_resume() -> T {
        auto handle = typed_handle();
        if (!handle || !handle.done()) {
            detail::async_task_error("gentest::awaitable resumed without a completed coroutine");
        }
        // Completion has transferred back to the parent, so removing the
        // scheduler's retained subtree cannot destroy a running child frame.
        if (handle.promise().scheduler) {
            handle.promise().scheduler->control()->cancel_waiters(handle);
        }
        detail::AsyncFrameScope::release_completed(frame_);
        if (handle.promise().exception) {
            std::rethrow_exception(handle.promise().exception);
        }
        if constexpr (!std::is_void_v<T>) {
            if (!handle.promise().value) {
                detail::async_task_error("gentest::awaitable resumed without return value");
            }
            return std::move(*handle.promise().value);
        }
    }

  private:
    [[nodiscard]] auto typed_handle() const noexcept -> std::coroutine_handle<promise_type> {
        return frame_ && frame_->handle ? std::coroutine_handle<promise_type>::from_address(frame_->handle.address())
                                        : std::coroutine_handle<promise_type>{};
    }

    void reset() noexcept {
        auto frame = std::move(frame_);
        if (frame) {
            frame->cancel();
        }
    }

    detail::AsyncFramePtr frame_;
};

} // namespace gentest

#include "gentest/detail/async_composition.h"

namespace gentest {

namespace detail {

template <typename T> [[nodiscard]] inline auto make_async_task(awaitable<T> operation) -> AsyncTaskPtr {
    return std::make_unique<awaitable<T>>(std::move(operation));
}

GENTEST_RUNTIME_API auto run_async_task_blocking(awaitable<void> task, std::string_view label, std::string &error_out) -> bool;

} // namespace detail

struct AsyncFixtureSetup {
    virtual ~AsyncFixtureSetup()    = default;
    virtual awaitable<void> setUp() = 0;
};

struct AsyncFixtureTearDown {
    virtual ~AsyncFixtureTearDown()    = default;
    virtual awaitable<void> tearDown() = 0;
};

} // namespace gentest
