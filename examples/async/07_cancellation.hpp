#pragma once

#include "gentest/async.h"
#include "gentest/test.h"

#include <chrono>
#include <coroutine>
#include <stop_token>
#include <utility>

namespace async_examples::cancellation {

// Model a backend that has registered a read but has no reply yet.
struct Backend {
    gentest::async::continuation<> callback;
    std::stop_token                stop;
    bool                           retired = false;
};

struct Read {
    explicit Read(Backend &source) : backend(source) {}
    Read(const Read &)                     = delete;
    auto operator=(const Read &) -> Read & = delete;
    Read(Read &&other) noexcept : backend(other.backend), registered(std::exchange(other.registered, false)) {}
    ~Read() { cancel_wait(); }

    Backend &backend;
    bool     registered = false;

    auto await_ready() const noexcept -> bool { return false; }

    template <typename Promise> void await_suspend(std::coroutine_handle<Promise> handle) {
        auto       next = handle.promise().make_continuation();
        const auto stop = next.stop_token();
        try {
            await_subscribe(next, stop);
        } catch (...) {
            next.cancel();
            throw;
        }
    }

    void await_subscribe(gentest::async::continuation_handle next, std::stop_token stop) {
        registered       = true;
        backend.stop     = std::move(stop);
        backend.callback = gentest::async::bind_continuation<>(std::move(next), []() noexcept {});
    }

    void await_resume() const noexcept {}

    // Called on the runner after timeout or successful extraction. Retire only
    // this registration; do not stop an executor shared with other cases.
    void cancel_wait() noexcept {
        if (std::exchange(registered, false)) {
            backend.callback = nullptr;
            backend.retired  = true;
        }
    }
};

[[gentest::test("retire_timed_out_registration")]]
inline auto retireTimedOutRegistration() -> gentest::async_test<void> {
    using namespace std::chrono_literals;
    Backend backend;

    auto result = co_await gentest::async::wait_for(Read{backend}, 1ms);
    gentest::expect_true(result.timed_out());
    if (backend.stop.stop_possible()) {
        gentest::expect_true(backend.stop.stop_requested());
        gentest::expect_true(backend.retired);
    } else {
        // An expired deadline can prevent registration from starting at all.
        gentest::expect_false(backend.retired);
    }
    gentest::expect_true(!backend.callback);

    Backend expired;
    auto    immediate = co_await gentest::async::wait_for(Read{expired}, 0ms);
    gentest::expect_true(immediate.timed_out());
    gentest::expect_false(expired.stop.stop_possible());
    gentest::expect_false(expired.retired);
    gentest::expect_true(!expired.callback);
}

} // namespace async_examples::cancellation
