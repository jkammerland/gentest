#include "gentest/runner.h"

#include <coroutine>
#include <future>
#include <thread>
#include <utility>

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

void leased_function_thread_entry(gentest::CurrentContext context) {
    auto lease = gentest::set_current_context(std::move(context));
    gentest::log("leased function thread");
}

void leased_thread_context() {
    auto        context = gentest::get_current_context();
    std::thread worker([context] {
        auto lease = gentest::set_current_context(context);
        gentest::log("leased thread");
    });
    worker.join();
}

void leased_stored_lambda_thread_context() {
    auto context     = gentest::get_current_context();
    auto worker_body = [context] {
        auto lease = gentest::set_current_context(context);
        gentest::log("leased stored lambda");
    };
    std::thread worker(worker_body);
    worker.join();
}

void leased_function_thread_context() {
    auto        context = gentest::get_current_context();
    std::thread worker(leased_function_thread_entry, context);
    worker.join();
}

void leased_async_context() {
    auto context = gentest::get_current_context();
    auto worker  = std::async(std::launch::async, [context] {
        auto lease = gentest::set_current_context(context);
        gentest::log("leased async");
    });
    worker.wait();
}

void deferred_async_uses_waiting_thread_context() {
    auto worker = std::async(std::launch::deferred, [] {});
    worker.wait();
}

auto leased_coroutine_context(gentest::CurrentContext context) -> DetachedTask {
    co_await std::suspend_always{};
    auto lease = gentest::set_current_context(std::move(context));
    gentest::log("leased coroutine");
    co_return;
}

} // namespace
