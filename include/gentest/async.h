#pragma once

#include "gentest/detail/runtime_base.h"

#include <condition_variable>
#include <coroutine>
#include <cstdlib>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gentest {

template <typename T = void> class async_test;

namespace detail {

class AsyncScheduler {
  public:
    virtual ~AsyncScheduler() = default;

    virtual void post(std::coroutine_handle<> handle)                                        = 0;
    virtual void block(std::coroutine_handle<> handle, std::string reason)                   = 0;
    virtual void attach_child(std::coroutine_handle<> child, std::coroutine_handle<> parent) = 0;
};

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
    [[nodiscard]] constexpr auto await_ready() const noexcept -> bool { return false; }

    void await_suspend(std::coroutine_handle<> handle) const {
        auto *scheduler = detail::current_async_scheduler();
        if (!scheduler) {
            std::abort();
        }
        scheduler->post(handle);
    }

    constexpr void await_resume() const noexcept {}
};

[[nodiscard]] inline auto yield() noexcept -> yield_awaitable { return {}; }

class manual_event {
  public:
    explicit manual_event(bool ready = false) : ready_(ready) {}

    void set() {
        std::vector<Waiter> waiters;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            ready_ = true;
            waiters.swap(waiters_);
        }
        for (auto &waiter : waiters) {
            if (waiter.scheduler) {
                waiter.scheduler->post(waiter.handle);
            }
        }
    }

    void reset() {
        std::lock_guard<std::mutex> lk(mtx_);
        ready_ = false;
    }

    [[nodiscard]] auto is_set() const -> bool {
        std::lock_guard<std::mutex> lk(mtx_);
        return ready_;
    }

    class awaitable {
      public:
        awaitable(manual_event &event, std::string reason) : event_(event), reason_(std::move(reason)) {}

        [[nodiscard]] auto await_ready() const -> bool {
            std::lock_guard<std::mutex> lk(event_.mtx_);
            return event_.ready_;
        }

        void await_suspend(std::coroutine_handle<> handle) {
            auto *scheduler = detail::current_async_scheduler();
            if (!scheduler) {
                std::abort();
            }
            std::lock_guard<std::mutex> lk(event_.mtx_);
            if (event_.ready_) {
                scheduler->post(handle);
                return;
            }
            event_.waiters_.push_back(Waiter{scheduler, handle});
            scheduler->block(handle, reason_);
        }

        constexpr void await_resume() const noexcept {}

      private:
        manual_event &event_;
        std::string   reason_;
    };

    [[nodiscard]] auto wait(std::string reason = "manual event was not signalled") -> awaitable {
        return awaitable{*this, std::move(reason)};
    }

  private:
    struct Waiter {
        detail::AsyncScheduler *scheduler = nullptr;
        std::coroutine_handle<> handle{};
    };

    mutable std::mutex  mtx_;
    bool                ready_ = false;
    std::vector<Waiter> waiters_;
};

class completion_source {
  public:
    void complete() { complete_impl({}); }

    void fail_unresumable(std::string reason) { complete_impl(std::move(reason)); }

    class awaitable {
      public:
        awaitable(completion_source &source, std::string reason) : source_(source), reason_(std::move(reason)) {}

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
            source_.waiters_.push_back(Waiter{scheduler, handle});
            scheduler->block(handle, reason_);
        }

        void await_resume() const {
            std::lock_guard<std::mutex> lk(source_.mtx_);
            if (!source_.unresumable_reason_.empty()) {
#if GENTEST_EXCEPTIONS_ENABLED
                throw detail::blocked_exception(source_.unresumable_reason_);
#else
                std::abort();
#endif
            }
        }

      private:
        completion_source &source_;
        std::string        reason_;
    };

    [[nodiscard]] auto wait(std::string reason = "completion source was not completed") -> awaitable {
        return awaitable{*this, std::move(reason)};
    }

  private:
    struct Waiter {
        detail::AsyncScheduler *scheduler = nullptr;
        std::coroutine_handle<> handle{};
    };

    void complete_impl(std::string unresumable_reason) {
        std::vector<Waiter> waiters;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            completed_          = true;
            unresumable_reason_ = std::move(unresumable_reason);
            waiters.swap(waiters_);
        }
        for (auto &waiter : waiters) {
            if (waiter.scheduler) {
                waiter.scheduler->post(waiter.handle);
            }
        }
    }

    mutable std::mutex  mtx_;
    bool                completed_ = false;
    std::string         unresumable_reason_;
    std::vector<Waiter> waiters_;
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
