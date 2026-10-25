#pragma once

// Included by async.h after its scheduler and thread-local access declarations.

#include <tuple>

namespace gentest::detail {
struct AsyncContinuationAccess;
}

namespace gentest::async {

class continuation_handle {
  public:
    continuation_handle() noexcept = default;
    continuation_handle(const continuation_handle &other) noexcept : token_(other.token_) {
        if (token_) {
            token_->retain_external();
        }
    }
    continuation_handle(continuation_handle &&) noexcept = default;
    auto operator=(continuation_handle other) noexcept -> continuation_handle & {
        token_.swap(other.token_);
        return *this;
    }
    ~continuation_handle() {
        if (token_) {
            token_->release_external();
        }
    }

    [[nodiscard]] explicit operator bool() const noexcept { return token_ && token_->active(); }
    void                   cancel() const noexcept {
        if (token_) {
            token_->cancel();
        }
    }
    [[nodiscard]] auto stop_token() const noexcept -> std::stop_token { return token_ ? token_->stop_token() : std::stop_token{}; }

    [[nodiscard]] auto try_invoke() const noexcept -> bool {
        return try_invoke([]() noexcept {});
    }

    template <typename Publish>
        requires std::is_nothrow_invocable_v<std::decay_t<Publish> &>
    [[nodiscard]] auto try_invoke(Publish &&publish) const noexcept -> bool {
        auto token = token_;
        return token && token->try_invoke(std::forward<Publish>(publish));
    }

    [[nodiscard]] auto operator()() const noexcept -> bool { return try_invoke(); }

  private:
    friend struct detail::AsyncContinuationAccess;
    explicit continuation_handle(detail::AsyncScheduler::WaiterTokenPtr token) noexcept : token_(std::move(token)) {
        if (token_) {
            token_->retain_external();
        }
    }

    detail::AsyncScheduler::WaiterTokenPtr token_;
};

template <typename... Arguments, typename Receiver>
    requires std::is_nothrow_invocable_v<std::decay_t<Receiver> &, std::decay_t<Arguments> &&...>
[[nodiscard]] auto bind_continuation(continuation_handle next, Receiver &&receiver) {
    // The receiver can capture awaiter storage, but workers never invoke it.
    auto bound = std::make_shared<std::decay_t<Receiver>>(std::forward<Receiver>(receiver));
    return [next = std::move(next), bound = std::move(bound)](Arguments... arguments) noexcept -> bool {
        // Own every argument before the provider's stack disappears. Reference
        // callback parameters are copied; explicit reference_wrapper stays borrowed.
        return next.try_invoke(
            [bound, values = std::tuple<std::decay_t<Arguments>...>(std::forward<Arguments>(arguments)...)]() mutable noexcept {
                std::apply([&bound](auto &&...values) noexcept { std::invoke(*bound, std::forward<decltype(values)>(values)...); },
                           std::move(values));
            });
    };
}

} // namespace gentest::async

namespace gentest::detail {
struct AsyncContinuationAccess {
    [[nodiscard]] static auto from_token(AsyncScheduler::WaiterTokenPtr token) noexcept -> async::continuation_handle {
        return async::continuation_handle{std::move(token)};
    }
};
} // namespace gentest::detail

namespace gentest::async {

[[nodiscard]] inline auto make_continuation(std::coroutine_handle<> handle, std::string reason = "custom asynchronous operation",
                                            std::source_location loc = std::source_location::current()) -> continuation_handle {
    auto *scheduler = detail::current_async_scheduler();
    if (!scheduler) {
        std::abort();
    }
    // One factory call issues a new generation. Copy that handle when several
    // callbacks compete to complete the same wait.
    scheduler->begin_wait(handle);
    auto token = scheduler->make_waiter(handle);
    scheduler->block_at(handle, std::move(reason), loc);
    return detail::AsyncContinuationAccess::from_token(std::move(token));
}

} // namespace gentest::async
