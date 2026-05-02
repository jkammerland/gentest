#pragma once

// Included by gentest/async.h after the async scheduler core has been declared.

namespace gentest::detail {

[[noreturn]] inline void async_contract_violation(std::string_view message) {
#if GENTEST_EXCEPTIONS_ENABLED
    throw std::logic_error(std::string(message));
#else
    (void)message;
    std::abort();
#endif
}

enum class AsyncPromiseTerminal {
    Pending,
    Value,
    Exception,
    Blocked,
};

struct AsyncPromiseWaitState {};

struct AsyncPromiseWaiter {
    AsyncScheduler::WaiterTokenPtr       token;
    std::weak_ptr<AsyncPromiseWaitState> state;
};

inline void post_async_promise_waiters(const std::vector<AsyncPromiseWaiter> &waiters) {
    for (auto &waiter : waiters) {
        if (auto state = waiter.state.lock()) {
            (void)state;
            if (waiter.token) {
                waiter.token->post();
            }
        } else if (waiter.token) {
            waiter.token->cancel();
        }
    }
}

[[nodiscard]] inline auto async_promise_blocked_reason(std::string reason) -> std::string {
    if (reason.empty()) {
        return "async promise cannot resume";
    }
    return reason;
}

[[nodiscard]] inline auto async_promise_wait_reason(std::string reason) -> std::string {
    if (reason.empty()) {
        return "async promise was not completed";
    }
    return reason;
}

template <typename T> class AsyncPromiseSharedState {
  public:
    static_assert(std::move_constructible<T>, "gentest::async::future<T> requires T to be move constructible");

    [[nodiscard]] auto try_mark_future_retrieved() -> bool {
        std::lock_guard<std::mutex> lk(mtx_);
        if (future_retrieved_) {
            return false;
        }
        future_retrieved_ = true;
        return true;
    }

    [[nodiscard]] auto try_mark_wait_started() -> bool {
        std::lock_guard<std::mutex> lk(mtx_);
        if (wait_started_) {
            return false;
        }
        wait_started_ = true;
        return true;
    }

    [[nodiscard]] auto is_pending() const -> bool {
        std::lock_guard<std::mutex> lk(mtx_);
        return terminal_ == AsyncPromiseTerminal::Pending;
    }

    [[nodiscard]] auto is_ready() const -> bool {
        std::lock_guard<std::mutex> lk(mtx_);
        return terminal_ != AsyncPromiseTerminal::Pending;
    }

    [[nodiscard]] auto wait_or_post(std::coroutine_handle<> handle, AsyncScheduler &scheduler,
                                    const std::shared_ptr<AsyncPromiseWaitState> &wait_state, std::string reason,
                                    const std::source_location &loc) -> bool {
        std::lock_guard<std::mutex> lk(mtx_);
        if (terminal_ != AsyncPromiseTerminal::Pending) {
            return true;
        }
        waiters_.push_back(AsyncPromiseWaiter{.token = scheduler.make_waiter(handle), .state = wait_state});
        scheduler.block_at(handle, async_promise_wait_reason(std::move(reason)), loc);
        return false;
    }

    [[nodiscard]] auto try_set_value(T value) -> bool {
        std::vector<AsyncPromiseWaiter> waiters;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (terminal_ != AsyncPromiseTerminal::Pending) {
                return false;
            }
            value_.emplace(std::move(value));
            terminal_ = AsyncPromiseTerminal::Value;
            waiters.swap(waiters_);
        }
        post_async_promise_waiters(waiters);
        return true;
    }

    [[nodiscard]] auto try_set_exception(std::exception_ptr exception) -> bool {
        std::vector<AsyncPromiseWaiter> waiters;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (terminal_ != AsyncPromiseTerminal::Pending) {
                return false;
            }
            exception_ = std::move(exception);
            terminal_  = AsyncPromiseTerminal::Exception;
            waiters.swap(waiters_);
        }
        post_async_promise_waiters(waiters);
        return true;
    }

    [[nodiscard]] auto try_set_blocked(std::string reason) -> bool {
        std::vector<AsyncPromiseWaiter> waiters;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (terminal_ != AsyncPromiseTerminal::Pending) {
                return false;
            }
            blocked_reason_ = async_promise_blocked_reason(std::move(reason));
            terminal_       = AsyncPromiseTerminal::Blocked;
            waiters.swap(waiters_);
        }
        post_async_promise_waiters(waiters);
        return true;
    }

    void abandon() noexcept { (void)try_set_blocked("async promise was abandoned before completion"); }

    [[nodiscard]] auto take_value() -> T {
        std::exception_ptr exception;
        std::string        blocked_reason;
        std::optional<T>   value;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            switch (terminal_) {
            case AsyncPromiseTerminal::Pending: async_contract_violation("gentest::async::future resumed before completion");
            case AsyncPromiseTerminal::Exception: exception = exception_; break;
            case AsyncPromiseTerminal::Blocked: blocked_reason = blocked_reason_; break;
            case AsyncPromiseTerminal::Value:
                if (consumed_ || !value_.has_value()) {
                    async_contract_violation("gentest::async::future value was already consumed");
                }
                value.emplace(std::move(*value_));
                consumed_ = true;
                break;
            }
        }
        if (exception) {
#if GENTEST_EXCEPTIONS_ENABLED
            std::rethrow_exception(exception);
#else
            std::abort();
#endif
        }
        if (!blocked_reason.empty()) {
#if GENTEST_EXCEPTIONS_ENABLED
            throw blocked_exception(std::move(blocked_reason));
#else
            std::abort();
#endif
        }
        if (!value.has_value()) {
            async_contract_violation("gentest::async::future resumed without value");
        }
        return std::move(value).value();
    }

  private:
    mutable std::mutex              mtx_;
    bool                            future_retrieved_ = false;
    bool                            wait_started_     = false;
    bool                            consumed_         = false;
    AsyncPromiseTerminal            terminal_         = AsyncPromiseTerminal::Pending;
    std::optional<T>                value_;
    std::exception_ptr              exception_;
    std::string                     blocked_reason_;
    std::vector<AsyncPromiseWaiter> waiters_;
};

template <> class AsyncPromiseSharedState<void> {
  public:
    [[nodiscard]] auto try_mark_future_retrieved() -> bool {
        std::lock_guard<std::mutex> lk(mtx_);
        if (future_retrieved_) {
            return false;
        }
        future_retrieved_ = true;
        return true;
    }

    [[nodiscard]] auto try_mark_wait_started() -> bool {
        std::lock_guard<std::mutex> lk(mtx_);
        if (wait_started_) {
            return false;
        }
        wait_started_ = true;
        return true;
    }

    [[nodiscard]] auto is_pending() const -> bool {
        std::lock_guard<std::mutex> lk(mtx_);
        return terminal_ == AsyncPromiseTerminal::Pending;
    }

    [[nodiscard]] auto is_ready() const -> bool {
        std::lock_guard<std::mutex> lk(mtx_);
        return terminal_ != AsyncPromiseTerminal::Pending;
    }

    [[nodiscard]] auto wait_or_post(std::coroutine_handle<> handle, AsyncScheduler &scheduler,
                                    const std::shared_ptr<AsyncPromiseWaitState> &wait_state, std::string reason,
                                    const std::source_location &loc) -> bool {
        std::lock_guard<std::mutex> lk(mtx_);
        if (terminal_ != AsyncPromiseTerminal::Pending) {
            return true;
        }
        waiters_.push_back(AsyncPromiseWaiter{.token = scheduler.make_waiter(handle), .state = wait_state});
        scheduler.block_at(handle, async_promise_wait_reason(std::move(reason)), loc);
        return false;
    }

    [[nodiscard]] auto try_set_value() -> bool {
        std::vector<AsyncPromiseWaiter> waiters;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (terminal_ != AsyncPromiseTerminal::Pending) {
                return false;
            }
            terminal_ = AsyncPromiseTerminal::Value;
            waiters.swap(waiters_);
        }
        post_async_promise_waiters(waiters);
        return true;
    }

    [[nodiscard]] auto try_set_exception(std::exception_ptr exception) -> bool {
        std::vector<AsyncPromiseWaiter> waiters;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (terminal_ != AsyncPromiseTerminal::Pending) {
                return false;
            }
            exception_ = std::move(exception);
            terminal_  = AsyncPromiseTerminal::Exception;
            waiters.swap(waiters_);
        }
        post_async_promise_waiters(waiters);
        return true;
    }

    [[nodiscard]] auto try_set_blocked(std::string reason) -> bool {
        std::vector<AsyncPromiseWaiter> waiters;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (terminal_ != AsyncPromiseTerminal::Pending) {
                return false;
            }
            blocked_reason_ = async_promise_blocked_reason(std::move(reason));
            terminal_       = AsyncPromiseTerminal::Blocked;
            waiters.swap(waiters_);
        }
        post_async_promise_waiters(waiters);
        return true;
    }

    void abandon() noexcept { (void)try_set_blocked("async promise was abandoned before completion"); }

    void take_value() {
        std::exception_ptr exception;
        std::string        blocked_reason;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            switch (terminal_) {
            case AsyncPromiseTerminal::Pending: async_contract_violation("gentest::async::future resumed before completion");
            case AsyncPromiseTerminal::Exception: exception = exception_; break;
            case AsyncPromiseTerminal::Blocked: blocked_reason = blocked_reason_; break;
            case AsyncPromiseTerminal::Value:
                if (consumed_) {
                    async_contract_violation("gentest::async::future value was already consumed");
                }
                consumed_ = true;
                break;
            }
        }
        if (exception) {
#if GENTEST_EXCEPTIONS_ENABLED
            std::rethrow_exception(exception);
#else
            std::abort();
#endif
        }
        if (!blocked_reason.empty()) {
#if GENTEST_EXCEPTIONS_ENABLED
            throw blocked_exception(std::move(blocked_reason));
#else
            std::abort();
#endif
        }
    }

  private:
    mutable std::mutex              mtx_;
    bool                            future_retrieved_ = false;
    bool                            wait_started_     = false;
    bool                            consumed_         = false;
    AsyncPromiseTerminal            terminal_         = AsyncPromiseTerminal::Pending;
    std::exception_ptr              exception_;
    std::string                     blocked_reason_;
    std::vector<AsyncPromiseWaiter> waiters_;
};

} // namespace gentest::detail

namespace gentest::async {

template <typename T> class promise;

template <typename T> class future {
  public:
    using value_type = T;

    future()                              = default;
    future(future &&) noexcept            = default;
    future &operator=(future &&) noexcept = default;

    future(const future &)            = delete;
    future &operator=(const future &) = delete;

    [[nodiscard]] auto valid() const noexcept -> bool { return static_cast<bool>(state_); }

    class awaitable {
      public:
        awaitable(std::shared_ptr<detail::AsyncPromiseSharedState<T>> state, std::string reason, std::source_location loc)
            : state_(std::move(state)), reason_(std::move(reason)), loc_(loc),
              wait_state_(std::make_shared<detail::AsyncPromiseWaitState>()) {}

        [[nodiscard]] auto await_ready() const -> bool { return state_->is_ready(); }

        void await_suspend(std::coroutine_handle<> handle) {
            auto *scheduler = detail::current_async_scheduler();
            if (!scheduler) {
                std::abort();
            }
            if (state_->wait_or_post(handle, *scheduler, wait_state_, reason_, loc_)) {
                scheduler->post(handle);
            }
        }

        [[nodiscard]] auto await_resume() const -> T { return state_->take_value(); }

      private:
        std::shared_ptr<detail::AsyncPromiseSharedState<T>> state_;
        std::string                                         reason_;
        std::source_location                                loc_;
        std::shared_ptr<detail::AsyncPromiseWaitState>      wait_state_;
    };

    [[nodiscard]] auto wait(std::string                 reason = "async promise was not completed",
                            const std::source_location &loc    = std::source_location::current()) -> awaitable {
        ensure_state();
        if (!state_->try_mark_wait_started()) {
            detail::async_contract_violation("gentest::async::future already has a waiter");
        }
        return awaitable{state_, std::move(reason), loc};
    }

    [[nodiscard]] auto operator co_await() & -> awaitable { return wait(); }
    [[nodiscard]] auto operator co_await() && -> awaitable { return wait(); }

  private:
    friend class promise<T>;

    explicit future(std::shared_ptr<detail::AsyncPromiseSharedState<T>> state) : state_(std::move(state)) {}

    void ensure_state() const {
        if (!state_) {
            detail::async_contract_violation("gentest::async::future has no state");
        }
    }

    std::shared_ptr<detail::AsyncPromiseSharedState<T>> state_;
};

template <typename T> class promise {
  public:
    using value_type = T;

    promise() : state_(std::make_shared<detail::AsyncPromiseSharedState<T>>()) {}

    promise(promise &&other) noexcept : state_(std::exchange(other.state_, {})) {}

    auto operator=(promise &&other) noexcept -> promise & {
        if (this != &other) {
            abandon();
            state_ = std::exchange(other.state_, {});
        }
        return *this;
    }

    promise(const promise &)            = delete;
    promise &operator=(const promise &) = delete;

    ~promise() { abandon(); }

    [[nodiscard]] auto get_future() -> future<T> {
        ensure_state();
        if (!state_->try_mark_future_retrieved()) {
            detail::async_contract_violation("gentest::async::promise future was already retrieved");
        }
        return future<T>{state_};
    }

    template <typename U>
        requires std::constructible_from<T, U &&>
    void set_value(U &&value) {
        if (!try_set_value(std::forward<U>(value))) {
            detail::async_contract_violation("gentest::async::promise was already satisfied or has no state");
        }
    }

    template <typename U>
        requires std::constructible_from<T, U &&>
    [[nodiscard]] auto try_set_value(U &&value) -> bool {
        if (!state_) {
            return false;
        }
        if (!state_->is_pending()) {
            return false;
        }
        return state_->try_set_value(T(std::forward<U>(value)));
    }

    void set_exception(std::exception_ptr exception) {
        if (!exception) {
            detail::async_contract_violation("gentest::async::promise exception cannot be null");
        }
        if (!try_set_exception(std::move(exception))) {
            detail::async_contract_violation("gentest::async::promise was already satisfied or has no state");
        }
    }

    [[nodiscard]] auto try_set_exception(std::exception_ptr exception) -> bool {
        if (!exception) {
            detail::async_contract_violation("gentest::async::promise exception cannot be null");
        }
        return state_ ? state_->try_set_exception(std::move(exception)) : false;
    }

    void set_blocked(std::string reason = {}) {
        if (!try_set_blocked(std::move(reason))) {
            detail::async_contract_violation("gentest::async::promise was already satisfied or has no state");
        }
    }

    [[nodiscard]] auto try_set_blocked(std::string reason = {}) -> bool {
        return state_ ? state_->try_set_blocked(std::move(reason)) : false;
    }

  private:
    void ensure_state() const {
        if (!state_) {
            detail::async_contract_violation("gentest::async::promise has no state");
        }
    }

    void abandon() noexcept {
        if (state_) {
            state_->abandon();
            state_.reset();
        }
    }

    std::shared_ptr<detail::AsyncPromiseSharedState<T>> state_;
};

template <> class future<void> {
  public:
    using value_type = void;

    future()                              = default;
    future(future &&) noexcept            = default;
    future &operator=(future &&) noexcept = default;

    future(const future &)            = delete;
    future &operator=(const future &) = delete;

    [[nodiscard]] auto valid() const noexcept -> bool { return static_cast<bool>(state_); }

    class awaitable {
      public:
        awaitable(std::shared_ptr<detail::AsyncPromiseSharedState<void>> state, std::string reason, std::source_location loc)
            : state_(std::move(state)), reason_(std::move(reason)), loc_(loc),
              wait_state_(std::make_shared<detail::AsyncPromiseWaitState>()) {}

        [[nodiscard]] auto await_ready() const -> bool { return state_->is_ready(); }

        void await_suspend(std::coroutine_handle<> handle) {
            auto *scheduler = detail::current_async_scheduler();
            if (!scheduler) {
                std::abort();
            }
            if (state_->wait_or_post(handle, *scheduler, wait_state_, reason_, loc_)) {
                scheduler->post(handle);
            }
        }

        void await_resume() const { state_->take_value(); }

      private:
        std::shared_ptr<detail::AsyncPromiseSharedState<void>> state_;
        std::string                                            reason_;
        std::source_location                                   loc_;
        std::shared_ptr<detail::AsyncPromiseWaitState>         wait_state_;
    };

    [[nodiscard]] auto wait(std::string                 reason = "async promise was not completed",
                            const std::source_location &loc    = std::source_location::current()) -> awaitable {
        ensure_state();
        if (!state_->try_mark_wait_started()) {
            detail::async_contract_violation("gentest::async::future already has a waiter");
        }
        return awaitable{state_, std::move(reason), loc};
    }

    [[nodiscard]] auto operator co_await() & -> awaitable { return wait(); }
    [[nodiscard]] auto operator co_await() && -> awaitable { return wait(); }

  private:
    friend class promise<void>;

    explicit future(std::shared_ptr<detail::AsyncPromiseSharedState<void>> state) : state_(std::move(state)) {}

    void ensure_state() const {
        if (!state_) {
            detail::async_contract_violation("gentest::async::future has no state");
        }
    }

    std::shared_ptr<detail::AsyncPromiseSharedState<void>> state_;
};

template <> class promise<void> {
  public:
    using value_type = void;

    promise() : state_(std::make_shared<detail::AsyncPromiseSharedState<void>>()) {}

    promise(promise &&other) noexcept : state_(std::exchange(other.state_, {})) {}

    auto operator=(promise &&other) noexcept -> promise & {
        if (this != &other) {
            abandon();
            state_ = std::exchange(other.state_, {});
        }
        return *this;
    }

    promise(const promise &)            = delete;
    promise &operator=(const promise &) = delete;

    ~promise() { abandon(); }

    [[nodiscard]] auto get_future() -> future<void> {
        ensure_state();
        if (!state_->try_mark_future_retrieved()) {
            detail::async_contract_violation("gentest::async::promise future was already retrieved");
        }
        return future<void>{state_};
    }

    void set_value() {
        if (!try_set_value()) {
            detail::async_contract_violation("gentest::async::promise was already satisfied or has no state");
        }
    }

    [[nodiscard]] auto try_set_value() -> bool { return state_ ? state_->try_set_value() : false; }

    void set_exception(std::exception_ptr exception) {
        if (!exception) {
            detail::async_contract_violation("gentest::async::promise exception cannot be null");
        }
        if (!try_set_exception(std::move(exception))) {
            detail::async_contract_violation("gentest::async::promise was already satisfied or has no state");
        }
    }

    [[nodiscard]] auto try_set_exception(std::exception_ptr exception) -> bool {
        if (!exception) {
            detail::async_contract_violation("gentest::async::promise exception cannot be null");
        }
        return state_ ? state_->try_set_exception(std::move(exception)) : false;
    }

    void set_blocked(std::string reason = {}) {
        if (!try_set_blocked(std::move(reason))) {
            detail::async_contract_violation("gentest::async::promise was already satisfied or has no state");
        }
    }

    [[nodiscard]] auto try_set_blocked(std::string reason = {}) -> bool {
        return state_ ? state_->try_set_blocked(std::move(reason)) : false;
    }

  private:
    void ensure_state() const {
        if (!state_) {
            detail::async_contract_violation("gentest::async::promise has no state");
        }
    }

    void abandon() noexcept {
        if (state_) {
            state_->abandon();
            state_.reset();
        }
    }

    std::shared_ptr<detail::AsyncPromiseSharedState<void>> state_;
};

} // namespace gentest::async
