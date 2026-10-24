#pragma once

#include "gentest/detail/async_api.h"
#include "gentest/detail/async_completion.h"
#include "gentest/detail/runtime_base.h"

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

template <typename T = void> class async_test;

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

struct yield_awaitable {
    using gentest_async_wait_supported = void;

    std::source_location loc;

    [[nodiscard]] constexpr auto await_ready() const noexcept -> bool { return false; }

    void await_suspend(std::coroutine_handle<> handle) const {
        auto *scheduler = detail::current_async_scheduler();
        if (!scheduler) {
            std::abort();
        }
        scheduler->begin_wait(handle);
        scheduler->yield_at(handle, loc);
    }

    void await_suspend_with_token(std::coroutine_handle<> handle, detail::AsyncScheduler &scheduler,
                                  const detail::AsyncScheduler::WaiterTokenPtr &token) const {
        scheduler.block_at(handle, "yielded cooperatively", loc);
        if (token) {
            token->post();
        }
    }

    constexpr void await_resume() const noexcept {}
};

[[nodiscard]] inline auto yield(const std::source_location &loc = std::source_location::current()) noexcept -> yield_awaitable {
    return yield_awaitable{.loc = loc};
}

class sleep_awaitable {
  public:
    using gentest_async_wait_supported = void;

    sleep_awaitable(std::chrono::steady_clock::time_point deadline, std::source_location loc) : deadline_(deadline), loc_(loc) {}

    [[nodiscard]] auto await_ready() const noexcept -> bool { return std::chrono::steady_clock::now() >= deadline_; }

    void await_suspend(std::coroutine_handle<> handle) {
        auto *scheduler = detail::current_async_scheduler();
        if (!scheduler) {
            std::abort();
        }
        scheduler->begin_wait(handle);
        await_suspend_with_token(handle, *scheduler, scheduler->make_waiter(handle));
    }

    void await_suspend_with_token(std::coroutine_handle<> handle, detail::AsyncScheduler &scheduler,
                                  const detail::AsyncScheduler::WaiterTokenPtr &token) {
        token_ = token;
        if (std::chrono::steady_clock::now() >= deadline_) {
            if (token_) {
                token_->post();
            }
            return;
        }
        scheduler.block_at(handle, "async timer has not elapsed", loc_);
        scheduler.schedule_timer(deadline_, token_);
    }

    constexpr void await_resume() const noexcept {}

    ~sleep_awaitable() {
        if (token_) {
            token_->cancel();
        }
    }

  private:
    std::chrono::steady_clock::time_point  deadline_;
    std::source_location                   loc_;
    detail::AsyncScheduler::WaiterTokenPtr token_;
};

template <typename Rep, typename Period>
[[nodiscard]] inline auto sleep_for(std::chrono::duration<Rep, Period> duration,
                                    const std::source_location        &loc = std::source_location::current()) -> sleep_awaitable {
    return sleep_awaitable{std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(duration),
                           loc};
}

template <typename Duration>
[[nodiscard]] inline auto sleep_until(std::chrono::time_point<std::chrono::steady_clock, Duration> deadline,
                                      const std::source_location &loc = std::source_location::current()) -> sleep_awaitable {
    return sleep_awaitable{std::chrono::time_point_cast<std::chrono::steady_clock::duration>(deadline), loc};
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
        using gentest_async_wait_supported = void;

        awaitable(event &owner, std::string key, std::source_location loc)
            : event_(owner), key_(std::move(key)), loc_(loc), state_(std::make_shared<WaitState>()) {}

        awaitable(awaitable &&other) noexcept
            : event_(other.event_), key_(std::move(other.key_)), loc_(other.loc_), state_(std::move(other.state_)),
              registered_(std::exchange(other.registered_, false)) {}

        awaitable &operator=(awaitable &&) noexcept = delete;
        awaitable(const awaitable &)                = delete;
        awaitable &operator=(const awaitable &)     = delete;

        ~awaitable() { cancel_wait(); }

        [[nodiscard]] auto await_ready() -> bool {
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
            scheduler->begin_wait(handle);
            await_suspend_with_token(handle, *scheduler, scheduler->make_waiter(handle));
        }

        void await_suspend_with_token(std::coroutine_handle<> handle, detail::AsyncScheduler &scheduler,
                                      const detail::AsyncScheduler::WaiterTokenPtr &token) {
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
                    scheduler.block_at(handle, event_.blocked_reason(key_), loc_);
                }
            }
            if (should_post) {
                if (token) {
                    token->post();
                } else {
                    scheduler.post(handle);
                }
            }
        }

        auto await_resume() const -> T & {
            if (!state_ || state_->value == nullptr) {
#if GENTEST_EXCEPTIONS_ENABLED
                throw std::runtime_error("gentest::async::event resumed without payload slot");
#else
                std::abort();
#endif
            }
            return *state_->value;
        }

        void cancel_wait(const detail::AsyncScheduler::Control &) noexcept { cancel_wait(); }

      private:
        void cancel_wait() noexcept {
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

        event                     &event_;
        std::string                key_;
        std::source_location       loc_;
        std::shared_ptr<WaitState> state_;
        bool                       registered_ = false;
    };

    [[nodiscard]] auto wait(std::string key, const std::source_location &loc = std::source_location::current()) -> awaitable {
        return awaitable{*this, std::move(key), loc};
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

    template <typename Promise> void await_suspend(std::coroutine_handle<Promise> handle) const noexcept {
        auto &promise = handle.promise();
        if (promise.continuation_token) {
            promise.continuation_token->post();
        } else if (promise.continuation && promise.scheduler) {
            promise.scheduler->post(promise.continuation);
        }
    }

    constexpr void await_resume() const noexcept {}
};

[[noreturn]] inline void reject_concurrent_async_test_await() {
#if GENTEST_EXCEPTIONS_ENABLED
    throw std::logic_error("gentest::async_test already has an active waiter (concurrent co_await is not supported)");
#else
    std::abort();
#endif
}

} // namespace detail

template <typename T> class async_test final : public detail::AsyncTask {
  public:
    using gentest_async_wait_supported = void;

    struct promise_type {
        detail::AsyncScheduler                *scheduler = nullptr;
        std::coroutine_handle<>                continuation{};
        detail::AsyncScheduler::WaiterTokenPtr continuation_token{};
        std::exception_ptr                     exception{};
        std::optional<T>                       value{};

        [[nodiscard]] auto make_continuation(std::source_location loc = std::source_location::current()) -> async::continuation_handle {
            return async::make_continuation(std::coroutine_handle<promise_type>::from_promise(*this), "custom asynchronous operation", loc);
        }

        [[nodiscard]] auto get_return_object() -> async_test {
            return async_test{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        [[nodiscard]] constexpr auto initial_suspend() const noexcept -> std::suspend_always { return {}; }
        [[nodiscard]] constexpr auto final_suspend() const noexcept -> detail::final_resume_awaiter { return {}; }

        template <typename U> void return_value(U &&v) { value.emplace(std::forward<U>(v)); }
        void                       unhandled_exception() noexcept { exception = std::current_exception(); }
    };

    async_test() = default;
    explicit async_test(std::coroutine_handle<promise_type> handle) : frame_(std::make_shared<detail::AsyncFrame>(handle)) {}
    async_test(async_test &&other) noexcept : frame_(std::move(other.frame_)) {}
    auto operator=(async_test &&other) noexcept -> async_test & {
        if (this != &other) {
            reset();
            frame_ = std::move(other.frame_);
        }
        return *this;
    }

    async_test(const async_test &)            = delete;
    async_test &operator=(const async_test &) = delete;

    ~async_test() override { reset(); }

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
        if (auto handle = typed_handle()) {
            return handle.promise().exception;
        }
        return {};
    }

    [[nodiscard]] auto await_ready() const noexcept -> bool { return !frame_ || frame_->done(); }

    void await_suspend(std::coroutine_handle<> continuation) {
        auto *scheduler = detail::current_async_scheduler();
        if (!scheduler || !frame_ || !frame_->handle) {
            std::abort();
        }
        scheduler->begin_wait(continuation);
        await_suspend_with_token(continuation, *scheduler, scheduler->make_waiter(continuation));
    }

    void await_suspend_with_token(std::coroutine_handle<> continuation, detail::AsyncScheduler &scheduler,
                                  const detail::AsyncScheduler::WaiterTokenPtr &token) {
        auto handle = typed_handle();
        if (!handle) {
            std::abort();
        }
        if (!frame_->try_claim_waiter()) {
            detail::reject_concurrent_async_test_await();
        }
        handle.promise().scheduler          = &scheduler;
        handle.promise().continuation       = continuation;
        handle.promise().continuation_token = token;
        scheduler.attach_child_frame(frame_, continuation);
        scheduler.post_frame(frame_);
    }

    void cancel_wait(const detail::AsyncScheduler::Control &control) noexcept {
        if (auto handle = typed_handle()) {
            control.cancel_waiters(handle);
            handle.promise().continuation_token.reset();
        }
    }

    auto await_resume() -> T {
        auto handle = typed_handle();
        if (!handle) {
#if GENTEST_EXCEPTIONS_ENABLED
            throw std::runtime_error("gentest::async_test resumed without coroutine state");
#else
            std::abort();
#endif
        }
        if (handle.promise().exception) {
            std::rethrow_exception(handle.promise().exception);
        }
        if (!handle.promise().value) {
#if GENTEST_EXCEPTIONS_ENABLED
            throw std::runtime_error("gentest::async_test resumed without return value");
#else
            std::abort();
#endif
        }
        return std::move(*handle.promise().value);
    }

  private:
    [[nodiscard]] auto typed_handle() const noexcept -> std::coroutine_handle<promise_type> {
        if (!frame_ || !frame_->handle) {
            return {};
        }
        return std::coroutine_handle<promise_type>::from_address(frame_->handle.address());
    }

    void reset() noexcept {
        // Stop callbacks may release or reset this task reentrantly.
        auto frame = std::move(frame_);
        if (frame) {
            frame->cancel();
        }
    }

    detail::AsyncFramePtr frame_;
};

template <> class async_test<void> final : public detail::AsyncTask {
  public:
    using gentest_async_wait_supported = void;

    struct promise_type {
        detail::AsyncScheduler                *scheduler = nullptr;
        std::coroutine_handle<>                continuation{};
        detail::AsyncScheduler::WaiterTokenPtr continuation_token{};
        std::exception_ptr                     exception{};

        [[nodiscard]] auto make_continuation(std::source_location loc = std::source_location::current()) -> async::continuation_handle {
            return async::make_continuation(std::coroutine_handle<promise_type>::from_promise(*this), "custom asynchronous operation", loc);
        }

        [[nodiscard]] auto get_return_object() -> async_test {
            return async_test{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        [[nodiscard]] constexpr auto initial_suspend() const noexcept -> std::suspend_always { return {}; }
        [[nodiscard]] constexpr auto final_suspend() const noexcept -> detail::final_resume_awaiter { return {}; }

        constexpr void return_void() const noexcept {}
        void           unhandled_exception() noexcept { exception = std::current_exception(); }
    };

    async_test() = default;
    explicit async_test(std::coroutine_handle<promise_type> handle) : frame_(std::make_shared<detail::AsyncFrame>(handle)) {}
    async_test(async_test &&other) noexcept : frame_(std::move(other.frame_)) {}
    auto operator=(async_test &&other) noexcept -> async_test & {
        if (this != &other) {
            reset();
            frame_ = std::move(other.frame_);
        }
        return *this;
    }

    async_test(const async_test &)            = delete;
    async_test &operator=(const async_test &) = delete;

    ~async_test() override { reset(); }

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
        if (auto handle = typed_handle()) {
            return handle.promise().exception;
        }
        return {};
    }

    [[nodiscard]] auto await_ready() const noexcept -> bool { return !frame_ || frame_->done(); }

    void await_suspend(std::coroutine_handle<> continuation) {
        auto *scheduler = detail::current_async_scheduler();
        if (!scheduler || !frame_ || !frame_->handle) {
            std::abort();
        }
        scheduler->begin_wait(continuation);
        await_suspend_with_token(continuation, *scheduler, scheduler->make_waiter(continuation));
    }

    void await_suspend_with_token(std::coroutine_handle<> continuation, detail::AsyncScheduler &scheduler,
                                  const detail::AsyncScheduler::WaiterTokenPtr &token) {
        auto handle = typed_handle();
        if (!handle) {
            std::abort();
        }
        if (!frame_->try_claim_waiter()) {
            detail::reject_concurrent_async_test_await();
        }
        handle.promise().scheduler          = &scheduler;
        handle.promise().continuation       = continuation;
        handle.promise().continuation_token = token;
        scheduler.attach_child_frame(frame_, continuation);
        scheduler.post_frame(frame_);
    }

    void cancel_wait(const detail::AsyncScheduler::Control &control) noexcept {
        if (auto handle = typed_handle()) {
            control.cancel_waiters(handle);
            handle.promise().continuation_token.reset();
        }
    }

    void await_resume() const {
        auto handle = typed_handle();
        if (!handle) {
#if GENTEST_EXCEPTIONS_ENABLED
            throw std::runtime_error("gentest::async_test resumed without coroutine state");
#else
            std::abort();
#endif
        }
        if (handle.promise().exception) {
            std::rethrow_exception(handle.promise().exception);
        }
    }

  private:
    [[nodiscard]] auto typed_handle() const noexcept -> std::coroutine_handle<promise_type> {
        if (!frame_ || !frame_->handle) {
            return {};
        }
        return std::coroutine_handle<promise_type>::from_address(frame_->handle.address());
    }

    void reset() noexcept {
        // Stop callbacks may release or reset this task reentrantly.
        auto frame = std::move(frame_);
        if (frame) {
            frame->cancel();
        }
    }

    detail::AsyncFramePtr frame_;
};

namespace async {
namespace timeout_detail {

template <typename T>
concept SubscribableAwaitable = requires(T &awaitable, continuation_handle next, std::stop_token stop) {
    { awaitable.await_ready() } -> std::convertible_to<bool>;
    { awaitable.await_subscribe(std::move(next), stop) } -> std::same_as<void>;
    awaitable.await_resume();
};

template <typename T>
concept SupportedTimedAwaitable =
    SubscribableAwaitable<T> || requires(T &awaitable, std::coroutine_handle<> handle, gentest::detail::AsyncScheduler &scheduler,
                                         gentest::detail::AsyncScheduler::WaiterTokenPtr token) {
        typename T::gentest_async_wait_supported;
        { awaitable.await_ready() } -> std::convertible_to<bool>;
        awaitable.await_suspend_with_token(handle, scheduler, std::move(token));
        awaitable.await_resume();
    };

template <typename T> inline constexpr bool always_false_v = false;

template <typename Awaitable> using await_resume_t = decltype(std::declval<Awaitable &>().await_resume());

template <typename Awaitable> struct timed_wait_result {
    using type = wait_result<await_resume_t<Awaitable>>;
};

template <typename Awaitable> struct timed_wait_result<Awaitable &> {
    using type = wait_result<await_resume_t<Awaitable>>;
};

struct timed_wait_state {
    enum class winner { none, ready, timeout };

    // Outcome publication is confined to the runner, like the awaiter's payload.
    [[nodiscard]] auto try_ready() noexcept -> bool {
        if (selected != winner::none) {
            return false;
        }
        selected = winner::ready;
        return true;
    }
    [[nodiscard]] auto try_timeout() noexcept -> bool {
        if (selected != winner::none) {
            return false;
        }
        selected = winner::timeout;
        return true;
    }
    [[nodiscard]] auto timed_out() const noexcept -> bool { return selected == winner::timeout; }

    winner selected = winner::none;
};

template <typename Awaitable>
void cancel_supported_awaitable(Awaitable &awaitable, const gentest::detail::AsyncScheduler::Control &control) noexcept {
    if constexpr (requires { awaitable.cancel_wait(); }) {
        static_assert(noexcept(awaitable.cancel_wait()), "custom cancel_wait() must be noexcept");
        awaitable.cancel_wait();
    } else if constexpr (requires { awaitable.cancel_wait(control); }) {
        awaitable.cancel_wait(control);
    }
}

template <SupportedTimedAwaitable Awaitable, typename Clock = std::chrono::steady_clock> class timeout_awaitable {
  public:
    using result_type = typename timed_wait_result<Awaitable>::type;

    timeout_awaitable(const Awaitable &awaitable, std::chrono::steady_clock::time_point deadline, std::source_location loc)
        requires std::copy_constructible<Awaitable>
        : awaitable_(awaitable), deadline_(deadline), loc_(loc) {}

    timeout_awaitable(Awaitable &&awaitable, std::chrono::steady_clock::time_point deadline, std::source_location loc)
        : awaitable_(std::move(awaitable)), deadline_(deadline), loc_(loc) {}

    timeout_awaitable(timeout_awaitable &&) noexcept            = default;
    timeout_awaitable &operator=(timeout_awaitable &&) noexcept = default;

    timeout_awaitable(const timeout_awaitable &)            = delete;
    timeout_awaitable &operator=(const timeout_awaitable &) = delete;

    ~timeout_awaitable() {
        cancel_tokens();
        if (inner_wait_started_ && control_) {
            if (ready_token_) {
                ready_token_->request_stop();
            }
            cancel_supported_awaitable(awaitable_, *control_);
        }
    }

    [[nodiscard]] auto await_ready() -> bool {
        if (awaitable_.await_ready()) {
            (void)state_->try_ready();
            return true;
        }
        if (Clock::now() >= deadline_) {
            (void)state_->try_timeout();
            return true;
        }
        return false;
    }

    void await_suspend(std::coroutine_handle<> handle) {
        auto *scheduler = gentest::detail::current_async_scheduler();
        if (!scheduler) {
            std::abort();
        }
        scheduler->begin_wait(handle);
        control_ = scheduler->control();
        if (Clock::now() >= deadline_) {
            (void)state_->try_timeout();
            scheduler->post(handle);
            return;
        }
        if (awaitable_.await_ready()) {
            (void)state_->try_ready();
            scheduler->post(handle);
            return;
        }

        ready_token_   = scheduler->make_waiter(handle);
        timeout_token_ = scheduler->make_waiter(handle);

        using ResumeReason = gentest::detail::AsyncScheduler::ResumeReason;
        auto complete = [state = state_, deadline = deadline_](ResumeReason reason, gentest::detail::AsyncTimePoint admitted) noexcept {
            if (reason == ResumeReason::Timeout || admitted >= deadline) {
                (void)state->try_timeout();
                return ResumeReason::Timeout;
            }
            (void)state->try_ready();
            return ResumeReason::Ready;
        };
        ready_token_->set_completion(complete, ResumeReason::Ready);
        timeout_token_->set_completion(std::move(complete), ResumeReason::Timeout);

        scheduler->schedule_timer(deadline_, timeout_token_);
        inner_wait_started_ = true;
#if GENTEST_EXCEPTIONS_ENABLED
        try {
#endif
            if constexpr (SubscribableAwaitable<Awaitable>) {
                scheduler->block_at(handle, "custom asynchronous operation", loc_);
                awaitable_.await_subscribe(gentest::detail::AsyncContinuationAccess::from_token(ready_token_), ready_token_->stop_token());
            } else {
                awaitable_.await_suspend_with_token(handle, *scheduler, ready_token_);
            }
#if GENTEST_EXCEPTIONS_ENABLED
        } catch (...) {
            // A named wrapper can outlive this exception. Roll back both
            // competitors now, before the coroutine enters another await.
            cancel_tokens();
            inner_wait_started_ = false;
            ready_token_->request_stop();
            cancel_supported_awaitable(awaitable_, *control_);
            throw;
        }
#endif
    }

    [[nodiscard]] auto await_resume() -> result_type {
        const bool timed_out = state_->timed_out();
        cancel_tokens();
        if (timed_out) {
            if (inner_wait_started_ && control_) {
                ready_token_->request_stop();
                cancel_supported_awaitable(awaitable_, *control_);
                inner_wait_started_ = false;
            }
            return result_type::make_timeout();
        }
        // A retirement hook may clear the awaiter's storage. Extract the
        // successful result before retiring the backend registration.
        auto result = [&]() -> result_type {
            if constexpr (std::is_void_v<await_resume_t<Awaitable>>) {
                awaitable_.await_resume();
                return result_type::make_ready();
            } else {
                return result_type::make_ready(awaitable_.await_resume());
            }
        }();
        if (inner_wait_started_ && control_) {
            cancel_supported_awaitable(awaitable_, *control_);
            inner_wait_started_ = false;
        }
        return result;
    }

  private:
    void cancel_tokens() noexcept {
        if (ready_token_) {
            ready_token_->cancel();
        }
        if (timeout_token_) {
            timeout_token_->cancel();
        }
    }

    Awaitable                                                 awaitable_;
    std::chrono::steady_clock::time_point                     deadline_;
    std::source_location                                      loc_;
    std::shared_ptr<timed_wait_state>                         state_ = std::make_shared<timed_wait_state>();
    std::shared_ptr<gentest::detail::AsyncScheduler::Control> control_;
    gentest::detail::AsyncScheduler::WaiterTokenPtr           ready_token_;
    gentest::detail::AsyncScheduler::WaiterTokenPtr           timeout_token_;
    bool                                                      inner_wait_started_ = false;
};

} // namespace timeout_detail

template <typename Awaitable, typename Duration>
    requires timeout_detail::SupportedTimedAwaitable<std::remove_cvref_t<Awaitable>>
[[nodiscard]] auto wait_until(Awaitable &&awaitable, std::chrono::time_point<std::chrono::steady_clock, Duration> deadline,
                              const std::source_location &loc = std::source_location::current()) {
    using Stored = std::remove_cvref_t<Awaitable>;
    return timeout_detail::timeout_awaitable<Stored>{std::forward<Awaitable>(awaitable),
                                                     std::chrono::time_point_cast<std::chrono::steady_clock::duration>(deadline), loc};
}

template <typename Awaitable, typename Duration>
    requires(!timeout_detail::SupportedTimedAwaitable<std::remove_cvref_t<Awaitable>>)
[[nodiscard]] auto wait_until(Awaitable &&, std::chrono::time_point<std::chrono::steady_clock, Duration>,
                              const std::source_location & = std::source_location::current()) {
    static_assert(timeout_detail::always_false_v<Awaitable>,
                  "gentest::async::wait_until requires a Gentest awaitable or a custom awaiter with "
                  "await_subscribe(continuation_handle, std::stop_token)");
}

template <typename Awaitable, typename Rep, typename Period>
    requires timeout_detail::SupportedTimedAwaitable<std::remove_cvref_t<Awaitable>>
[[nodiscard]] auto wait_for(Awaitable &&awaitable, std::chrono::duration<Rep, Period> duration,
                            const std::source_location &loc = std::source_location::current()) {
    return wait_until(std::forward<Awaitable>(awaitable),
                      std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(duration), loc);
}

template <typename Awaitable, typename Rep, typename Period>
    requires(!timeout_detail::SupportedTimedAwaitable<std::remove_cvref_t<Awaitable>>)
[[nodiscard]] auto wait_for(Awaitable &&, std::chrono::duration<Rep, Period>,
                            const std::source_location & = std::source_location::current()) {
    static_assert(timeout_detail::always_false_v<Awaitable>,
                  "gentest::async::wait_for requires a Gentest awaitable or a custom awaiter with "
                  "await_subscribe(continuation_handle, std::stop_token)");
}

} // namespace async

namespace detail {

template <typename T> [[nodiscard]] inline auto make_async_task(async_test<T> task) -> AsyncTaskPtr {
    return std::make_unique<async_test<T>>(std::move(task));
}

GENTEST_RUNTIME_API auto run_async_task_blocking(async_test<void> task, std::string_view label, std::string &error_out) -> bool;

} // namespace detail

struct AsyncFixtureSetup {
    virtual ~AsyncFixtureSetup()     = default;
    virtual async_test<void> setUp() = 0;
};

struct AsyncFixtureTearDown {
    virtual ~AsyncFixtureTearDown()     = default;
    virtual async_test<void> tearDown() = 0;
};

} // namespace gentest
