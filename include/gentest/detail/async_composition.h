#pragma once

// Included by async.h after awaitable and wait_result have been defined.

#include <array>
#include <tuple>

namespace gentest::detail {

// Match the language's member, ADL, and direct-awaiter resolution order.
template <typename Input> decltype(auto) resolve_async_awaiter(Input &&input) {
    if constexpr (requires { std::forward<Input>(input).operator co_await(); }) {
        return std::forward<Input>(input).operator co_await();
    } else if constexpr (requires { operator co_await(std::forward<Input>(input)); }) {
        return operator co_await(std::forward<Input>(input));
    } else {
        return std::forward<Input>(input);
    }
}

template <typename Input> using AsyncStoredInput = std::conditional_t<std::is_lvalue_reference_v<Input>, Input, std::remove_cvref_t<Input>>;

template <typename Input> using AsyncInputExpression = std::conditional_t<std::is_lvalue_reference_v<Input>, Input, Input &&>;

template <typename Awaiter, bool = std::is_reference_v<Awaiter>> class AsyncAwaiterStorage {
  public:
    // Direct initialization from the factory preserves guaranteed copy elision,
    // including an immovable awaiter returned by operator co_await.
    template <typename Factory>
        requires(!std::same_as<std::remove_cvref_t<Factory>, AsyncAwaiterStorage>)
    explicit AsyncAwaiterStorage(Factory &&factory) : awaiter_(std::forward<Factory>(factory)()) {}

    auto get() noexcept -> Awaiter & { return awaiter_; }

  private:
    Awaiter awaiter_;
};

template <typename Awaiter> class AsyncAwaiterStorage<Awaiter, true> {
  public:
    template <typename Factory>
        requires(!std::same_as<std::remove_cvref_t<Factory>, AsyncAwaiterStorage>)
    explicit AsyncAwaiterStorage(Factory &&factory) {
        auto &&awaiter = std::forward<Factory>(factory)();
        awaiter_       = std::addressof(awaiter);
    }

    auto get() const noexcept -> std::remove_reference_t<Awaiter> & { return *awaiter_; }

  private:
    std::remove_reference_t<Awaiter> *awaiter_;
};

template <typename Input> class AsyncCompositionInput {
    using Resolved = decltype(resolve_async_awaiter(std::declval<AsyncInputExpression<Input>>()));

  public:
    using Awaiter    = std::remove_reference_t<Resolved>;
    using Result     = decltype(std::declval<Awaiter &>().await_resume());
    using Value      = std::conditional_t<std::is_void_v<Result>, std::monostate, std::decay_t<Result>>;
    using TimedValue = std::conditional_t<std::is_lvalue_reference_v<Result>, Result, std::remove_cvref_t<Result>>;

    template <typename Value>
        requires(!std::same_as<std::remove_cvref_t<Value>, AsyncCompositionInput>)
    explicit AsyncCompositionInput(Value &&value)
        : input_(std::forward<Value>(value)), awaiter_([this]() -> decltype(auto) { return resolve_async_awaiter(input_expression()); }) {}

    // A resolved awaiter can borrow this input, so its address must stay fixed.
    AsyncCompositionInput(const AsyncCompositionInput &)                     = delete;
    AsyncCompositionInput(AsyncCompositionInput &&)                          = delete;
    auto operator=(const AsyncCompositionInput &) -> AsyncCompositionInput & = delete;
    auto operator=(AsyncCompositionInput &&) -> AsyncCompositionInput &      = delete;

    auto awaiter() noexcept -> Awaiter & { return awaiter_.get(); }

    auto ready() -> bool {
        if (!checked_) {
            ready_   = static_cast<bool>(awaiter().await_ready());
            checked_ = true;
        }
        return ready_;
    }

  private:
    auto input_expression() noexcept -> AsyncInputExpression<Input> { return static_cast<AsyncInputExpression<Input>>(input_); }

    Input                         input_;
    AsyncAwaiterStorage<Resolved> awaiter_;
    bool                          checked_ = false;
    bool                          ready_   = false;
};

enum class AsyncCompositionKind { All, Any, Timed };

template <AsyncCompositionKind Kind, typename... Input> struct AsyncCompositionResult;

template <typename... Input> struct AsyncCompositionResult<AsyncCompositionKind::All, Input...> {
    using Type = std::tuple<typename AsyncCompositionInput<Input>::Value...>;
};

template <typename... Input> struct AsyncCompositionResult<AsyncCompositionKind::Any, Input...> {
    using Type = std::variant<typename AsyncCompositionInput<Input>::Value...>;
};

template <typename Input> struct AsyncCompositionResult<AsyncCompositionKind::Timed, Input> {
    using Type = async::wait_result<typename AsyncCompositionInput<Input>::TimedValue>;
};

template <AsyncCompositionKind Kind, typename Clock, typename... Input>
class AsyncCompositionState : public std::enable_shared_from_this<AsyncCompositionState<Kind, Clock, Input...>> {
    using Self                         = AsyncCompositionState;
    static constexpr std::size_t count = sizeof...(Input);
    static constexpr bool        timed = Kind == AsyncCompositionKind::Timed;

    template <typename Item>
    using StoredResult = std::conditional_t<timed, async::wait_result<typename AsyncCompositionInput<Item>::TimedValue>,
                                            typename AsyncCompositionInput<Item>::Value>;

    enum class Selection { None, Ready, Timeout };

  public:
    using Result = typename AsyncCompositionResult<Kind, Input...>::Type;

    template <typename... Value>
    explicit AsyncCompositionState(AsyncTimePoint deadline, std::source_location loc, Value &&...value)
        : inputs_(std::make_unique<AsyncCompositionInput<Input>>(std::forward<Value>(value))...), deadline_(deadline), loc_(loc) {}

    ~AsyncCompositionState() {
        // Capabilities are invalidated before the awaiter storage they protect.
        if (parent_) {
            parent_->cancel();
        }
        if (timer_) {
            timer_->cancel();
        }
        release_adapters();
    }

    auto ready() -> bool {
        if constexpr (timed) {
            if (input_ready<0>()) {
                selection_ = Selection::Ready;
                return true;
            }
            if (Clock::now() >= deadline_) {
                selection_ = Selection::Timeout;
                return true;
            }
        }
        return count == 0;
    }

    auto start(std::coroutine_handle<> parent) -> bool {
        auto *scheduler = current_async_scheduler();
        if (!scheduler) {
            std::abort();
        }
        if constexpr (timed) {
            // No operation registration occurs if the deadline passed between
            // await_ready and await_suspend.
            if (Clock::now() >= deadline_) {
                selection_ = Selection::Timeout;
                return false;
            }
        }

#if GENTEST_EXCEPTIONS_ENABLED
        try {
#endif
            scheduler->begin_wait(parent);
            control_ = scheduler->control();
            parent_  = scheduler->make_waiter(parent);
            scheduler->block_at(parent, "waiting for asynchronous composition", loc_);

            auto weak    = this->weak_from_this();
            parent_stop_ = std::make_unique<ParentStop>(parent_->stop_token(), [weak]() noexcept {
                if (auto state = weak.lock()) {
                    state->stop_.request_stop();
                }
            });

            // Resolve and allocate every adapter before any input can start.
            prepare_inputs(*scheduler, parent, std::index_sequence_for<Input...>{});
            if constexpr (timed) {
                adapters_[count] = timer_adapter();
                adapters_[count].set_scheduler(scheduler);
                adapters_[count].frame()->set_stop_token(std::stop_token{});
                timer_promise().on_complete = [weak]() noexcept {
                    if (auto state = weak.lock()) {
                        state->timer_completed();
                    }
                };
                scheduler->attach_child_frame(adapters_[count].frame(), parent);
                timer_ = scheduler->make_waiter(adapters_[count].handle());
                timer_->set_completion(
                    [weak](AsyncResumeReason reason, AsyncTimePoint) noexcept {
                        if (auto state = weak.lock()) {
                            state->select_timeout();
                        }
                        return reason;
                    },
                    AsyncResumeReason::Timeout);
                scheduler->schedule_timer(deadline_, timer_);
            }

            started_ = true;
            for (std::size_t index = 0; index != count; ++index) {
                scheduler->post_frame(adapters_[index].frame());
            }
            return true;
#if GENTEST_EXCEPTIONS_ENABLED
        } catch (...) {
            // A named wrapper can survive a failed setup. Revoke all issued
            // capabilities now, before its parent starts another suspension.
            if (parent_) {
                parent_->cancel();
            }
            if (timer_) {
                timer_->cancel();
            }
            release_adapters();
            throw;
        }
#endif
    }

    auto result() -> Result {
        // Result construction can throw, including while moving a selected
        // value. Joined work must be retired even for a named wrapper.
        struct Release {
            Self &state;
            bool  preserve_reference = false;

            ~Release() {
                state.release_adapters();
                if (!preserve_reference) {
                    state.release_inputs();
                }
            }
        } release{*this};

        if constexpr (timed) {
            if (selection_ == Selection::Timeout) {
                return Result::make_timeout();
            }
            if (!started_) {
                auto result                = ready_result();
                release.preserve_reference = true;
                return result;
            }
        }

        if (exception_) {
            auto exception = exception_;
#if GENTEST_EXCEPTIONS_ENABLED
            std::rethrow_exception(exception);
#else
            std::abort();
#endif
        }

        auto result = [&]() -> Result {
            if constexpr (timed) {
                return std::move(stored_value<0>());
            } else if constexpr (Kind == AsyncCompositionKind::All) {
                return tuple_result(std::index_sequence_for<Input...>{});
            } else {
                return variant_result<0>();
            }
        }();
        release.preserve_reference = true;
        return result;
    }

  private:
    using ParentStop = std::stop_callback<std::function<void()>>;

    // Defer tuple indexing until this helper is instantiated. MSVC otherwise
    // checks get<0> in ready's discarded timed branch for an empty when_all.
    template <std::size_t Index> auto input_ready() -> bool { return std::get<Index>(inputs_)->ready(); }

    template <std::size_t Index> class ObservedAwaiter {
      public:
        explicit ObservedAwaiter(Self &state) noexcept : state_(state) {}

        auto await_ready() -> bool { return std::get<Index>(state_.inputs_)->ready(); }

        template <typename Promise> auto await_suspend(std::coroutine_handle<Promise> handle) -> std::coroutine_handle<> {
            if constexpr (timed) {
                if (Clock::now() >= state_.deadline_) {
                    state_.select_timeout();
                    skipped_ = true;
                    return handle;
                }

                auto weak = state_.weak_from_this();
                state_.adapters_[Index].frame()->observe_next_completion(
                    [weak](AsyncResumeReason reason, AsyncTimePoint admitted) noexcept {
                        if (auto state = weak.lock()) {
                            state->observe_completion(reason, admitted);
                        }
                    });
            }
            auto &awaiter = std::get<Index>(state_.inputs_)->awaiter();
            using Suspend = decltype(awaiter.await_suspend(handle));
            if constexpr (std::is_void_v<Suspend>) {
                awaiter.await_suspend(handle);
                return std::noop_coroutine();
            } else if constexpr (std::same_as<Suspend, bool>) {
                if (awaiter.await_suspend(handle)) {
                    return std::noop_coroutine();
                }
                return handle;
            } else {
                return awaiter.await_suspend(handle);
            }
        }

        decltype(auto) await_resume() {
            if constexpr (timed) {
                if (skipped_) {
#if GENTEST_EXCEPTIONS_ENABLED
                    throw async::operation_cancelled{};
#else
                    std::abort();
#endif
                }
                // await_suspend(false) and synchronous symmetric transfers do
                // not necessarily pass through the scheduler's ready queue.
                state_.adapters_[Index].frame()->notify_completion(AsyncResumeReason::Ready, Clock::now());
            }
            return std::get<Index>(state_.inputs_)->awaiter().await_resume();
        }

      private:
        Self &state_;
        bool  skipped_ = false;
    };

    template <std::size_t Index> static auto input_adapter(Self *state) -> awaitable<void> {
        using Item = std::tuple_element_t<Index, std::tuple<Input...>>;
        using Raw  = typename AsyncCompositionInput<Item>::Result;
#if GENTEST_EXCEPTIONS_ENABLED
        try {
#endif
            if constexpr (timed) {
                // The runner may not enter this prepared adapter until after
                // the deadline. Do not register a pending backend at that point.
                if (!std::get<Index>(state->inputs_)->ready() && Clock::now() >= state->deadline_) {
                    state->select_timeout();
                    co_return;
                }
            }

            if constexpr (std::is_void_v<Raw>) {
                co_await ObservedAwaiter<Index>{*state};
                if constexpr (timed) {
                    std::get<Index>(state->values_).emplace(StoredResult<Item>::make_ready());
                } else {
                    std::get<Index>(state->values_).emplace();
                }
            } else {
                if constexpr (timed) {
                    std::get<Index>(state->values_).emplace(StoredResult<Item>::make_ready(co_await ObservedAwaiter<Index>{*state}));
                } else {
                    std::get<Index>(state->values_).emplace(co_await ObservedAwaiter<Index>{*state});
                }
            }
#if GENTEST_EXCEPTIONS_ENABLED
        } catch (...) { state->errors_[Index] = std::current_exception(); }
#endif
    }

    static auto timer_adapter() -> awaitable<void> { co_return; }

    auto timer_promise() -> awaitable<void>::promise_type & {
        return std::coroutine_handle<awaitable<void>::promise_type>::from_address(adapters_[count].handle().address()).promise();
    }

    template <std::size_t... Index>
    void prepare_inputs(AsyncScheduler &scheduler, std::coroutine_handle<> parent, std::index_sequence<Index...>) {
        (prepare_input<Index>(scheduler, parent), ...);
    }

    template <std::size_t Index> void prepare_input(AsyncScheduler &scheduler, std::coroutine_handle<> parent) {
        adapters_[Index] = input_adapter<Index>(this);
        auto &adapter    = adapters_[Index];
        adapter.set_scheduler(&scheduler);
        adapter.frame()->set_stop_token(stop_.get_token());
        // Ready delivery pins the state before either callback publication or
        // coroutine code runs. The weak dependency creates no suspended cycle.
        adapter.frame()->set_execution_lifetime(this->weak_from_this());
        auto &promise       = std::coroutine_handle<awaitable<void>::promise_type>::from_address(adapter.handle().address()).promise();
        promise.on_complete = [weak = this->weak_from_this()]() noexcept {
            if (auto state = weak.lock()) {
                state->template input_completed<Index>();
            }
        };
        scheduler.attach_child_frame(adapter.frame(), parent);
    }

    template <std::size_t Index> void input_completed() noexcept {
        if constexpr (Kind == AsyncCompositionKind::Any) {
            if (selection_ == Selection::None) {
                selection_ = Selection::Ready;
                winner_    = Index;
                exception_ = errors_[Index];
                stop_.request_stop();
            }
        } else if constexpr (timed) {
            if (selection_ == Selection::None) {
                observe_completion(AsyncResumeReason::Ready, Clock::now());
            }
            if (selection_ == Selection::Ready) {
                exception_ = errors_[Index];
            }
            // Retire the internal timer even when readiness won far before its
            // deadline. A second queued timer receipt cannot claim its frame.
            timer_->post();
        } else if (errors_[Index] && !exception_) {
            exception_ = errors_[Index];
        }

        --remaining_;
        publish_join();
    }

    void observe_completion(AsyncResumeReason reason, AsyncTimePoint admitted) noexcept {
        if (selection_ != Selection::None) {
            return;
        }
        if (reason == AsyncResumeReason::Timeout || admitted >= deadline_) {
            select_timeout();
        } else {
            selection_ = Selection::Ready;
        }
    }

    void select_timeout() noexcept {
        if (selection_ == Selection::None) {
            selection_ = Selection::Timeout;
            stop_.request_stop();
        }
    }

    void timer_completed() noexcept {
        --remaining_;
        publish_join();
    }

    void publish_join() noexcept {
        if (remaining_ == 0 && parent_) {
            parent_->post();
        }
    }

    auto ready_result() -> Result
        requires(timed)
    {
        using Raw = typename AsyncCompositionInput<std::tuple_element_t<0, std::tuple<Input...>>>::Result;
        if constexpr (std::is_void_v<Raw>) {
            std::get<0>(inputs_)->awaiter().await_resume();
            return Result::make_ready();
        } else {
            return Result::make_ready(std::get<0>(inputs_)->awaiter().await_resume());
        }
    }

    template <std::size_t Index> auto stored_value() -> StoredResult<std::tuple_element_t<Index, std::tuple<Input...>>> & {
        auto &value = std::get<Index>(values_);
        if (!value) {
            std::abort();
        }
        return *value;
    }

    template <std::size_t... Index> auto tuple_result(std::index_sequence<Index...>) -> Result {
        return Result{std::move(stored_value<Index>())...};
    }

    template <std::size_t Index> auto variant_result() -> Result {
        if (winner_ == Index) {
            return Result{std::in_place_index<Index>, std::move(stored_value<Index>())};
        }
        if constexpr (Index + 1 < count) {
            return variant_result<Index + 1>();
        } else {
            std::abort();
        }
    }

    void release_adapters() noexcept {
        for (auto &adapter : adapters_) {
            if (control_ && adapter.handle()) {
                // Scheduler registrations retain native descendants during a
                // transfer. A completed join retires that whole owned subtree.
                control_->cancel_waiters(adapter.handle());
            }
            AsyncFrameScope::release_completed(adapter.frame());
            adapter = {};
        }
        if constexpr (!timed || (!std::is_lvalue_reference_v<typename AsyncCompositionInput<Input>::Result> && ...)) {
            release_inputs();
        }
        // Named combinators must not retain unselected values or exceptions
        // after the join has returned its owning result.
        std::apply([](auto &...value) { (value.reset(), ...); }, values_);
        errors_    = {};
        exception_ = {};
        parent_stop_.reset();
    }

    void release_inputs() noexcept {
        std::apply([](auto &...input) { (input.reset(), ...); }, inputs_);
    }

    std::tuple<std::unique_ptr<AsyncCompositionInput<Input>>...> inputs_;
    std::tuple<std::optional<StoredResult<Input>>...>            values_;
    std::array<awaitable<void>, count + (timed ? 1 : 0)>         adapters_;
    std::array<std::exception_ptr, count>                        errors_{};
    AsyncTimePoint                                               deadline_;
    std::source_location                                         loc_;
    std::stop_source                                             stop_;
    std::unique_ptr<ParentStop>                                  parent_stop_;
    std::shared_ptr<AsyncScheduler::Control>                     control_;
    AsyncScheduler::WaiterTokenPtr                               parent_;
    AsyncScheduler::WaiterTokenPtr                               timer_;
    std::exception_ptr                                           exception_;
    std::size_t                                                  remaining_ = count + (timed ? 1 : 0);
    std::size_t                                                  winner_    = 0;
    Selection                                                    selection_ = Selection::None;
    bool                                                         started_   = false;
};

template <AsyncCompositionKind Kind, typename Clock, typename... Input> class AsyncCompositionAwaitable {
    using State = AsyncCompositionState<Kind, Clock, Input...>;

  public:
    template <typename... Value>
    explicit AsyncCompositionAwaitable(AsyncTimePoint deadline, std::source_location loc, Value &&...value)
        : state_(std::make_shared<State>(deadline, loc, std::forward<Value>(value)...)) {}

    AsyncCompositionAwaitable(AsyncCompositionAwaitable &&) noexcept                     = default;
    auto operator=(AsyncCompositionAwaitable &&) noexcept -> AsyncCompositionAwaitable & = default;
    AsyncCompositionAwaitable(const AsyncCompositionAwaitable &)                         = delete;
    auto operator=(const AsyncCompositionAwaitable &) -> AsyncCompositionAwaitable &     = delete;

    auto await_ready() -> bool { return state_->ready(); }
    auto await_suspend(std::coroutine_handle<> parent) -> bool { return state_->start(parent); }
    auto await_resume() -> typename State::Result { return state_->result(); }

  private:
    std::shared_ptr<State> state_;
};

} // namespace gentest::detail

namespace gentest::async {

namespace timeout_detail {

// Kept private so regression schedulers can supply the same deterministic clock.
template <typename Awaitable, typename Clock = std::chrono::steady_clock>
class timeout_awaitable
    : public detail::AsyncCompositionAwaitable<detail::AsyncCompositionKind::Timed, Clock, detail::AsyncStoredInput<Awaitable>> {
    using Base = detail::AsyncCompositionAwaitable<detail::AsyncCompositionKind::Timed, Clock, detail::AsyncStoredInput<Awaitable>>;

  public:
    template <typename Value>
    timeout_awaitable(Value &&value, detail::AsyncTimePoint deadline, std::source_location loc)
        : Base(deadline, loc, std::forward<Value>(value)) {}
};

} // namespace timeout_detail

template <typename... Input> [[nodiscard]] auto when_all(Input &&...input) {
    return detail::AsyncCompositionAwaitable<detail::AsyncCompositionKind::All, std::chrono::steady_clock,
                                             detail::AsyncStoredInput<Input>...>{{},
                                                                                 std::source_location::current(),
                                                                                 std::forward<Input>(input)...};
}

template <typename... Input>
    requires(sizeof...(Input) != 0)
[[nodiscard]] auto when_any(Input &&...input) {
    static_assert(GENTEST_EXCEPTIONS_ENABLED || sizeof...(Input) == 0,
                  "gentest::async::when_any requires exception support for cancellation and joining");
    return detail::AsyncCompositionAwaitable<detail::AsyncCompositionKind::Any, std::chrono::steady_clock,
                                             detail::AsyncStoredInput<Input>...>{{},
                                                                                 std::source_location::current(),
                                                                                 std::forward<Input>(input)...};
}

template <typename Awaitable, typename Duration>
[[nodiscard]] auto wait_until(Awaitable &&awaitable, std::chrono::time_point<std::chrono::steady_clock, Duration> deadline,
                              const std::source_location &loc = std::source_location::current()) {
    static_assert(GENTEST_EXCEPTIONS_ENABLED || !std::is_same_v<Awaitable, Awaitable>,
                  "gentest::async::wait_until and wait_for require exception support for cancellation and joining");
    return timeout_detail::timeout_awaitable<Awaitable>{std::forward<Awaitable>(awaitable),
                                                        std::chrono::time_point_cast<std::chrono::steady_clock::duration>(deadline), loc};
}

template <typename Awaitable, typename Rep, typename Period>
[[nodiscard]] auto wait_for(Awaitable &&awaitable, std::chrono::duration<Rep, Period> duration,
                            const std::source_location &loc = std::source_location::current()) {
    return wait_until(std::forward<Awaitable>(awaitable),
                      std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(duration), loc);
}

} // namespace gentest::async
