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

void adopted_function_thread_entry(gentest::CurrentContext context) {
    auto adoption = gentest::set_current_context(std::move(context));
    gentest::expect(true);
}

void adopted_thread_context() {
    auto        context = gentest::get_current_context();
    std::thread worker([context] {
        auto adoption = gentest::set_current_context(context);
        gentest::log("adopted thread");
        EXPECT_TRUE(true);
    });
    worker.join();
}

void adopted_stored_lambda_thread_context() {
    auto context     = gentest::get_current_context();
    auto worker_body = [context] {
        auto adoption = gentest::set_current_context(context);
        gentest::expect(true);
    };
    std::thread worker(worker_body);
    worker.join();
}

void adopted_function_thread_context() {
    auto        context = gentest::get_current_context();
    std::thread worker(adopted_function_thread_entry, context);
    worker.join();
}

void adopted_async_context() {
    auto context = gentest::get_current_context();
    auto worker  = std::async(std::launch::async, [context] {
        auto adoption = gentest::set_current_context(context);
        gentest::expect(true);
    });
    worker.wait();
}

void deferred_async_uses_waiting_thread_context() {
    auto worker = std::async(std::launch::deferred, [] { gentest::expect(true); });
    worker.wait();
}

auto adopted_coroutine_context(gentest::CurrentContext context) -> DetachedTask {
    co_await std::suspend_always{};
    auto adoption = gentest::set_current_context(std::move(context));
    gentest::require(true);
    co_return;
}

} // namespace
