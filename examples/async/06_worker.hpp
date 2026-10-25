#pragma once

#include "gentest/async.h"
#include "gentest/fixture.h"
#include "gentest/test.h"

#include <condition_variable>
#include <coroutine>
#include <deque>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <utility>

namespace async_examples::worker {

using namespace gentest;
using namespace gentest::async;

// A complete, small backend. No Gentest registration or loop interface is needed.
class Loop {
  public:
    Loop() : worker_([this] { run(); }) {}
    Loop(const Loop &)                     = delete;
    auto operator=(const Loop &) -> Loop & = delete;

    ~Loop() {
        {
            std::lock_guard lock(mutex_);
            closing_ = true;
        }
        ready_.notify_one();
        worker_.join();
    }

    void post(continuation<> work) {
        {
            std::lock_guard lock(mutex_);
            queue_.push_back(std::move(work));
        }
        ready_.notify_one();
    }

  private:
    void run() {
        while (true) {
            continuation<> work;
            {
                std::unique_lock lock(mutex_);
                ready_.wait(lock, [this] { return closing_ || !queue_.empty(); });
                if (queue_.empty()) {
                    return;
                }
                work = std::move(queue_.front());
                queue_.pop_front();
            }
            work();
        }
    }

    std::mutex                 mutex_;
    std::condition_variable    ready_;
    std::deque<continuation<>> queue_;
    bool                       closing_ = false;
    std::thread                worker_;
};

enum class Reply { value, unavailable };

// Ordinary C++ awaiter, with an optional subscription hook for wait_for/until.
class Read {
  public:
    explicit Read(Loop &loop, Reply reply = Reply::value) : loop_(loop), reply_(reply) {}

    auto await_ready() const noexcept -> bool { return false; }

    template <typename Promise> void await_suspend(std::coroutine_handle<Promise> handle) {
        auto       next = handle.promise().make_continuation();
        const auto stop = next.stop_token();
        try {
            await_subscribe(next, stop);
        } catch (...) {
            // Registration can have queued an inline callback before failing.
            next.cancel();
            throw;
        }
    }

    void await_subscribe(continuation_handle next, std::stop_token stop) {
        auto complete =
            bind_continuation<Reply, std::unique_ptr<int>>(std::move(next), [this](Reply reply, std::unique_ptr<int> value) noexcept {
                reply_ = reply;
                value_ = std::move(value);
            });

        // Only the owned reply crosses threads. Gentest invokes the receiver
        // above and resumes the coroutine on its runner thread.
        loop_.post([complete = std::move(complete), stop = std::move(stop), reply = reply_]() mutable {
            if (!stop.stop_requested()) {
                (void)complete(reply, reply == Reply::value ? std::make_unique<int>(42) : nullptr);
            }
        });
    }

    auto await_resume() -> std::unique_ptr<int> {
        if (reply_ != Reply::value || !value_) {
            throw std::runtime_error("backend could not read a value");
        }
        return std::move(value_);
    }

  private:
    Loop                &loop_;
    Reply                reply_;
    std::unique_ptr<int> value_;
};

struct [[gentest::fixture(suite)]] Infrastructure {
    Loop loop;
};

[[gentest::test("callback_value_on_runner")]]
inline auto callbackValueOnRunner(Infrastructure &infrastructure) -> async_test<void> {
    const auto runner = std::this_thread::get_id();
    auto       value  = co_await Read{infrastructure.loop};
    expect_eq(*value, 42);
    expect_true(std::this_thread::get_id() == runner);
}

[[gentest::test("bounded_callback")]]
inline auto boundedCallback(Infrastructure &infrastructure) -> async_test<void> {
    using namespace std::chrono_literals;
    auto result = co_await wait_for(Read{infrastructure.loop}, 5s);
    if (result.timed_out()) {
        fail("backend did not complete the read");
        co_return;
    }
    expect_eq(*result.value(), 42);
}

[[gentest::test("backend_error")]]
inline auto backendError(Infrastructure &infrastructure) -> async_test<void> {
    bool rejected = false;
    try {
        (void)co_await Read{infrastructure.loop, Reply::unavailable};
    } catch (const std::runtime_error &) { rejected = true; }
    expect_true(rejected);
}

} // namespace async_examples::worker
