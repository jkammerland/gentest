#include "gentest/runner.h"

#include <coroutine>
#include <future>
#include <thread>
#include <utility>

using namespace gentest::asserts;

namespace {

struct DetachedTask {
    struct promise_type {
        auto get_return_object() -> DetachedTask { return {}; }
        auto initial_suspend() noexcept -> std::suspend_never { return {}; }
        auto final_suspend() noexcept -> std::suspend_never { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept {}
    };
};

void adopted_function_thread_entry(gentest::CurrentToken token) {
    auto adoption = gentest::set_current_token(std::move(token));
    gentest::expect(true);
}

void adopted_thread_token() {
    auto        token = gentest::get_current_token();
    std::thread worker([token] {
        auto adoption = gentest::set_current_token(token);
        gentest::log("adopted thread");
        EXPECT_TRUE(true);
    });
    worker.join();
}

void adopted_stored_lambda_thread_token() {
    auto token       = gentest::get_current_token();
    auto worker_body = [token] {
        auto adoption = gentest::set_current_token(token);
        gentest::expect(true);
    };
    std::thread worker(worker_body);
    worker.join();
}

void adopted_function_thread_token() {
    auto        token = gentest::get_current_token();
    std::thread worker(adopted_function_thread_entry, token);
    worker.join();
}

void adopted_async_token() {
    auto token  = gentest::get_current_token();
    auto worker = std::async(std::launch::async, [token] {
        auto adoption = gentest::set_current_token(token);
        gentest::expect(true);
    });
    worker.wait();
}

void deferred_async_uses_waiting_thread_token() {
    auto worker = std::async(std::launch::deferred, [] { gentest::expect(true); });
    worker.wait();
}

auto adopted_coroutine_token(gentest::CurrentToken token) -> DetachedTask {
    co_await std::suspend_always{};
    auto adoption = gentest::set_current_token(std::move(token));
    gentest::require(true);
    co_return;
}

} // namespace
