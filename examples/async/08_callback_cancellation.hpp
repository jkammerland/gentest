#pragma once

#include "gentest/async.h"
#include "gentest/test.h"

#include <chrono>
#include <coroutine>
#include <functional>
#include <memory>
#include <stop_token>
#include <utility>

namespace async_examples::cancellation {

using namespace gentest;
using namespace gentest::async;

// A runner-thread backend with one pending read and no incoming reply.
struct Backend {
    std::function<bool()> callback;
    std::stop_token       stop;
    bool                  acknowledged = false;
    bool                  retired      = false;
};

class Read {
  public:
    explicit Read(Backend &backend) : backend_(backend) {}
    Read(const Read &)                     = delete;
    auto operator=(const Read &) -> Read & = delete;
    Read(Read &&other) noexcept
        : backend_(other.backend_), cancellation_(std::move(other.cancellation_)), registered_(std::exchange(other.registered_, false)) {}
    ~Read() { retire(); }

    auto await_ready() const noexcept -> bool { return false; }

    template <typename Promise> void await_suspend(std::coroutine_handle<Promise> handle) {
        auto next = handle.promise().make_continuation();
        try {
            backend_.stop     = next.stop_token();
            backend_.callback = next;
            registered_       = true;

            // Acknowledge stop by completing the read. Clearing its callback
            // alone would leave the coroutine suspended and its join unfinished.
            cancellation_ = std::make_unique<StopCallback>(backend_.stop, [backend = &backend_] {
                backend->acknowledged = true;
                (void)backend->callback();
            });
        } catch (...) {
            retire();
            next.cancel();
            throw;
        }
    }

    void await_resume() const { throw operation_cancelled{}; }

  private:
    using StopCallback = std::stop_callback<std::function<void()>>;

    // Scoped registration cleanup runs after completion, including cancellation.
    void retire() noexcept {
        cancellation_.reset();
        if (std::exchange(registered_, false)) {
            backend_.callback = nullptr;
            backend_.retired  = true;
        }
    }

    Backend                      &backend_;
    std::unique_ptr<StopCallback> cancellation_;
    bool                          registered_ = false;
};

[[gentest::test("stop_and_join_registration")]]
inline auto stopAndJoinRegistration() -> task<> {
    using namespace std::chrono_literals;
    Backend backend;

    // The read registers first. Yield finishes next and requests its stop.
    auto result = co_await when_any(Read{backend}, yield());
    expect_eq(result.index(), std::size_t{1});
    expect_true(backend.stop.stop_requested());
    expect_true(backend.acknowledged);
    expect_true(backend.retired);
    expect_true(!backend.callback);

    // An expired deadline needs no registration for an operation that is not ready.
    Backend expired;
    auto    immediate = co_await wait_for(Read{expired}, 0ms);
    expect_true(immediate.timed_out());
    expect_false(expired.stop.stop_possible());
    expect_false(expired.retired);
    expect_true(!expired.callback);
}

} // namespace async_examples::cancellation
