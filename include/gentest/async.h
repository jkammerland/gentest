#pragma once

#include "gentest/detail/runtime_base.h"

#include <any>
#include <condition_variable>
#include <coroutine>
#include <cstdlib>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gentest {

template <typename T = void> class async_test;

namespace detail {

class AsyncScheduler {
  public:
    class Control {
      public:
        void post(std::coroutine_handle<> handle) const;

      private:
        friend class AsyncScheduler;

        mutable std::mutex mtx_;
        AsyncScheduler    *scheduler_ = nullptr;
    };

    class WaiterToken {
      public:
        WaiterToken(std::weak_ptr<Control> control, std::coroutine_handle<> handle) : control_(std::move(control)), handle_(handle) {}

        void post() const;
        void cancel() noexcept;

      private:
        mutable std::mutex      mtx_;
        std::weak_ptr<Control>  control_;
        std::coroutine_handle<> handle_{};
        bool                    active_ = true;
    };

    using WaiterTokenPtr = std::shared_ptr<WaiterToken>;

    AsyncScheduler() : control_(std::make_shared<Control>()) { control_->scheduler_ = this; }
    virtual ~AsyncScheduler() { deactivate(); }

    virtual void post(std::coroutine_handle<> handle)                                        = 0;
    virtual void block(std::coroutine_handle<> handle, std::string reason)                   = 0;
    virtual void attach_child(std::coroutine_handle<> child, std::coroutine_handle<> parent) = 0;

    virtual void block_at(std::coroutine_handle<> handle, std::string reason, const std::source_location &) {
        block(handle, std::move(reason));
    }

    virtual void yield_at(std::coroutine_handle<> handle, const std::source_location &) { post(handle); }

    [[nodiscard]] auto control() const noexcept -> std::shared_ptr<Control> { return control_; }

    [[nodiscard]] virtual auto make_waiter(std::coroutine_handle<> handle) -> WaiterTokenPtr {
        return std::make_shared<WaiterToken>(control_, handle);
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

inline void AsyncScheduler::WaiterToken::post() const {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!active_) {
        return;
    }
    if (auto control = control_.lock()) {
        control->post(handle_);
    }
}

inline void AsyncScheduler::WaiterToken::cancel() noexcept {
    std::lock_guard<std::mutex> lk(mtx_);
    active_ = false;
    handle_ = {};
    control_.reset();
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
    virtual void               set_scheduler(AsyncScheduler *scheduler) noexcept  = 0;
    [[nodiscard]] virtual auto exception() const noexcept -> std::exception_ptr   = 0;
};

using AsyncTaskPtr = std::unique_ptr<AsyncTask>;
using AsyncCaseFn  = AsyncTaskPtr (*)(void *);

} // namespace detail

namespace async {

struct yield_awaitable {
    std::source_location loc;

    [[nodiscard]] constexpr auto await_ready() const noexcept -> bool { return false; }

    void await_suspend(std::coroutine_handle<> handle) const {
        auto *scheduler = detail::current_async_scheduler();
        if (!scheduler) {
            std::abort();
        }
        scheduler->yield_at(handle, loc);
    }

    constexpr void await_resume() const noexcept {}
};

[[nodiscard]] inline auto yield(const std::source_location &loc = std::source_location::current()) noexcept -> yield_awaitable {
    return yield_awaitable{.loc = loc};
}

class manual_event {
    struct Slot;

    struct WaitState {
        Slot *slot = nullptr;
    };

    struct Waiter {
        detail::AsyncScheduler::WaiterTokenPtr token;
        std::weak_ptr<WaitState>               state;
    };

    struct Slot {
        bool                ready = false;
        std::any            value;
        std::vector<Waiter> waiters;
    };

  public:
    manual_event() = default;

    void set(std::string key, std::any &&value = {}) {
        std::vector<Waiter> waiters;
        Slot               *slot_ptr = nullptr;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            auto                       &slot = slots_[std::move(key)];
            slot.value                       = std::move(value);
            slot.ready                       = true;
            slot_ptr                         = &slot;
            waiters.swap(slot.waiters);
        }
        for (auto &waiter : waiters) {
            if (auto state = waiter.state.lock()) {
                state->slot = slot_ptr;
                if (waiter.token) {
                    waiter.token->post();
                }
            } else if (waiter.token) {
                waiter.token->cancel();
            }
        }
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
        awaitable(manual_event &event, std::string key, std::source_location loc)
            : event_(event), key_(std::move(key)), loc_(loc), state_(std::make_shared<WaitState>()) {}

        [[nodiscard]] auto await_ready() -> bool {
            std::lock_guard<std::mutex> lk(event_.mtx_);
            auto                        it = event_.slots_.find(key_);
            if (it == event_.slots_.end() || !it->second.ready) {
                return false;
            }
            state_->slot = &it->second;
            return true;
        }

        void await_suspend(std::coroutine_handle<> handle) {
            auto *scheduler = detail::current_async_scheduler();
            if (!scheduler) {
                std::abort();
            }
            bool should_post = false;
            {
                std::lock_guard<std::mutex> lk(event_.mtx_);
                auto                       &slot = event_.slots_[key_];
                if (slot.ready) {
                    state_->slot = &slot;
                    should_post  = true;
                } else {
                    slot.waiters.push_back(Waiter{.token = scheduler->make_waiter(handle), .state = state_});
                    scheduler->block_at(handle, event_.blocked_reason(key_), loc_);
                }
            }
            if (should_post) {
                scheduler->post(handle);
            }
        }

        auto await_resume() const -> std::any & {
            if (!state_ || state_->slot == nullptr) {
#if GENTEST_EXCEPTIONS_ENABLED
                throw std::runtime_error("gentest::manual_event resumed without payload slot");
#else
                std::abort();
#endif
            }
            return state_->slot->value;
        }

      private:
        manual_event              &event_;
        std::string                key_;
        std::source_location       loc_;
        std::shared_ptr<WaitState> state_;
    };

    [[nodiscard]] auto wait(std::string key, const std::source_location &loc = std::source_location::current()) -> awaitable {
        return awaitable{*this, std::move(key), loc};
    }

  private:
    [[nodiscard]] static auto blocked_reason(std::string_view key) -> std::string {
        if (key.empty()) {
            return "manual event key was not set";
        }
        std::string reason = "manual event key '";
        reason += key;
        reason += "' was not set";
        return reason;
    }

    mutable std::mutex                    mtx_;
    std::unordered_map<std::string, Slot> slots_;
};

class completion_source {
  public:
    void complete() { complete_impl(false, {}); }

    void fail_unresumable(std::string reason = {}) { complete_impl(true, std::move(reason)); }

    class awaitable {
      public:
        awaitable(completion_source &source, std::string reason, std::source_location loc)
            : source_(source), reason_(std::move(reason)), loc_(loc) {}

        [[nodiscard]] auto await_ready() const -> bool {
            std::lock_guard<std::mutex> lk(source_.mtx_);
            return source_.completed_;
        }

        void await_suspend(std::coroutine_handle<> handle) {
            auto *scheduler = detail::current_async_scheduler();
            if (!scheduler) {
                std::abort();
            }
            std::lock_guard<std::mutex> lk(source_.mtx_);
            if (source_.completed_) {
                scheduler->post(handle);
                return;
            }
            source_.waiters_.push_back(scheduler->make_waiter(handle));
            scheduler->block_at(handle, reason_, loc_);
        }

        void await_resume() const {
            std::lock_guard<std::mutex> lk(source_.mtx_);
            if (source_.unresumable_) {
#if GENTEST_EXCEPTIONS_ENABLED
                throw detail::blocked_exception(source_.unresumable_reason_);
#else
                std::abort();
#endif
            }
        }

      private:
        completion_source   &source_;
        std::string          reason_;
        std::source_location loc_;
    };

    [[nodiscard]] auto wait(std::string                 reason = "completion source was not completed",
                            const std::source_location &loc    = std::source_location::current()) -> awaitable {
        return awaitable{*this, std::move(reason), loc};
    }

  private:
    void complete_impl(bool unresumable, std::string unresumable_reason) {
        std::vector<detail::AsyncScheduler::WaiterTokenPtr> waiters;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (completed_) {
                return;
            }
            completed_   = true;
            unresumable_ = unresumable;
            unresumable_reason_ =
                unresumable && unresumable_reason.empty() ? std::string("completion source cannot resume") : std::move(unresumable_reason);
            waiters.swap(waiters_);
        }
        for (auto &waiter : waiters) {
            if (waiter) {
                waiter->post();
            }
        }
    }

    mutable std::mutex                                  mtx_;
    bool                                                completed_   = false;
    bool                                                unresumable_ = false;
    std::string                                         unresumable_reason_;
    std::vector<detail::AsyncScheduler::WaiterTokenPtr> waiters_;
};

} // namespace async

namespace detail {

struct final_resume_awaiter {
    [[nodiscard]] constexpr auto await_ready() const noexcept -> bool { return false; }

    template <typename Promise> void await_suspend(std::coroutine_handle<Promise> handle) const noexcept {
        auto &promise = handle.promise();
        if (promise.continuation && promise.scheduler) {
            promise.scheduler->post(promise.continuation);
        }
    }

    constexpr void await_resume() const noexcept {}
};

} // namespace detail

template <typename T> class async_test final : public detail::AsyncTask {
  public:
    struct promise_type {
        detail::AsyncScheduler *scheduler = nullptr;
        std::coroutine_handle<> continuation{};
        std::exception_ptr      exception{};
        std::optional<T>        value{};

        [[nodiscard]] auto get_return_object() noexcept -> async_test {
            return async_test{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        [[nodiscard]] constexpr auto initial_suspend() const noexcept -> std::suspend_always { return {}; }
        [[nodiscard]] constexpr auto final_suspend() const noexcept -> detail::final_resume_awaiter { return {}; }

        template <typename U> void return_value(U &&v) { value.emplace(std::forward<U>(v)); }
        void                       unhandled_exception() noexcept { exception = std::current_exception(); }
    };

    async_test() = default;
    explicit async_test(std::coroutine_handle<promise_type> handle) noexcept : handle_(handle) {}
    async_test(async_test &&other) noexcept : handle_(std::exchange(other.handle_, {})) {}
    auto operator=(async_test &&other) noexcept -> async_test & {
        if (this != &other) {
            reset();
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }

    async_test(const async_test &)            = delete;
    async_test &operator=(const async_test &) = delete;

    ~async_test() override { reset(); }

    [[nodiscard]] auto handle() const noexcept -> std::coroutine_handle<> override { return handle_; }

    void set_scheduler(detail::AsyncScheduler *scheduler) noexcept override {
        if (handle_) {
            handle_.promise().scheduler = scheduler;
        }
    }

    [[nodiscard]] auto exception() const noexcept -> std::exception_ptr override {
        return handle_ ? handle_.promise().exception : std::exception_ptr{};
    }

    [[nodiscard]] auto await_ready() const noexcept -> bool { return !handle_ || handle_.done(); }

    void await_suspend(std::coroutine_handle<> continuation) {
        auto *scheduler = detail::current_async_scheduler();
        if (!scheduler || !handle_) {
            std::abort();
        }
        handle_.promise().scheduler    = scheduler;
        handle_.promise().continuation = continuation;
        scheduler->attach_child(handle_, continuation);
        scheduler->post(handle_);
    }

    auto await_resume() -> T {
        if (!handle_) {
#if GENTEST_EXCEPTIONS_ENABLED
            throw std::runtime_error("gentest::async_test resumed without coroutine state");
#else
            std::abort();
#endif
        }
        if (handle_.promise().exception) {
            std::rethrow_exception(handle_.promise().exception);
        }
        if (!handle_.promise().value) {
#if GENTEST_EXCEPTIONS_ENABLED
            throw std::runtime_error("gentest::async_test resumed without return value");
#else
            std::abort();
#endif
        }
        return std::move(*handle_.promise().value);
    }

  private:
    void reset() noexcept {
        if (handle_) {
            handle_.destroy();
            handle_ = {};
        }
    }

    std::coroutine_handle<promise_type> handle_{};
};

template <> class async_test<void> final : public detail::AsyncTask {
  public:
    struct promise_type {
        detail::AsyncScheduler *scheduler = nullptr;
        std::coroutine_handle<> continuation{};
        std::exception_ptr      exception{};

        [[nodiscard]] auto get_return_object() noexcept -> async_test {
            return async_test{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        [[nodiscard]] constexpr auto initial_suspend() const noexcept -> std::suspend_always { return {}; }
        [[nodiscard]] constexpr auto final_suspend() const noexcept -> detail::final_resume_awaiter { return {}; }

        constexpr void return_void() const noexcept {}
        void           unhandled_exception() noexcept { exception = std::current_exception(); }
    };

    async_test() = default;
    explicit async_test(std::coroutine_handle<promise_type> handle) noexcept : handle_(handle) {}
    async_test(async_test &&other) noexcept : handle_(std::exchange(other.handle_, {})) {}
    auto operator=(async_test &&other) noexcept -> async_test & {
        if (this != &other) {
            reset();
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }

    async_test(const async_test &)            = delete;
    async_test &operator=(const async_test &) = delete;

    ~async_test() override { reset(); }

    [[nodiscard]] auto handle() const noexcept -> std::coroutine_handle<> override { return handle_; }

    void set_scheduler(detail::AsyncScheduler *scheduler) noexcept override {
        if (handle_) {
            handle_.promise().scheduler = scheduler;
        }
    }

    [[nodiscard]] auto exception() const noexcept -> std::exception_ptr override {
        return handle_ ? handle_.promise().exception : std::exception_ptr{};
    }

    [[nodiscard]] auto await_ready() const noexcept -> bool { return !handle_ || handle_.done(); }

    void await_suspend(std::coroutine_handle<> continuation) {
        auto *scheduler = detail::current_async_scheduler();
        if (!scheduler || !handle_) {
            std::abort();
        }
        handle_.promise().scheduler    = scheduler;
        handle_.promise().continuation = continuation;
        scheduler->attach_child(handle_, continuation);
        scheduler->post(handle_);
    }

    void await_resume() const {
        if (!handle_) {
#if GENTEST_EXCEPTIONS_ENABLED
            throw std::runtime_error("gentest::async_test resumed without coroutine state");
#else
            std::abort();
#endif
        }
        if (handle_.promise().exception) {
            std::rethrow_exception(handle_.promise().exception);
        }
    }

  private:
    void reset() noexcept {
        if (handle_) {
            handle_.destroy();
            handle_ = {};
        }
    }

    std::coroutine_handle<promise_type> handle_{};
};

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
