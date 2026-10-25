#pragma once

// Included after the scheduler and continuation declarations in async.h.
namespace gentest::async {

class operation_cancelled : public std::exception {
  public:
    [[nodiscard]] auto what() const noexcept -> const char * override { return "asynchronous operation cancelled"; }
};

} // namespace gentest::async

namespace gentest::detail {

class AsyncWaitCancellation {
  public:
    explicit AsyncWaitCancellation(std::optional<std::stop_token> override_token = {}) : override_(std::move(override_token)) {}

    void arm(const AsyncScheduler::WaiterTokenPtr &continuation) {
        const auto token = override_.value_or(continuation->stop_token());
        if (!token.stop_possible()) {
            return;
        }

        // Stop callbacks may run on a worker. They only mark cancellation and
        // enqueue a guarded resume; await_resume performs cleanup on the runner.
        cancelled_ = std::make_shared<std::atomic_bool>(false);
        callback_  = std::make_unique<std::stop_callback<std::function<void()>>>(token, [cancelled = cancelled_, continuation]() noexcept {
            cancelled->store(true, std::memory_order_release);
            continuation->post();
        });
    }

    void check() const {
        const auto *frame   = current_async_frame();
        const bool  stopped = override_ ? override_->stop_requested() : frame && frame->stop_token().stop_requested();
        if ((cancelled_ && cancelled_->load(std::memory_order_acquire)) || stopped) {
#if GENTEST_EXCEPTIONS_ENABLED
            throw async::operation_cancelled{};
#else
            std::abort();
#endif
        }
    }

  private:
    std::optional<std::stop_token>                             override_;
    std::shared_ptr<std::atomic_bool>                          cancelled_;
    std::unique_ptr<std::stop_callback<std::function<void()>>> callback_;
};

} // namespace gentest::detail
