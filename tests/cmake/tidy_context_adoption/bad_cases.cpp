#include "gentest/runner.h"
#include "header_cases.hpp"

#include <coroutine>
#include <future>
#include <thread>

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

void function_thread_entry_without_context() { gentest::log("missing function thread adoption"); }

void missing_thread_context() {
    header_tidy_context_adoption::missing_header_thread_context();

    std::thread worker([] {
        gentest::log("missing thread adoption");
        EXPECT_TRUE(true);
    });
    worker.join();
}

void missing_async_context() {
    auto worker = std::async(std::launch::async, [] { gentest::expect(true); });
    worker.wait();
}

void stored_lambda_thread_context() {
    auto        worker_body = [] { gentest::expect(true); };
    std::thread worker(worker_body);
    worker.join();
}

void function_thread_context() {
    std::thread worker(function_thread_entry_without_context);
    worker.join();
}

void discarded_thread_context() {
    auto        context = gentest::get_current_context();
    std::thread worker([context] {
        static_cast<void>(gentest::set_current_context(context));
        gentest::log("discarded adoption");
    });
    worker.join();
}

void expired_scope_thread_context() {
    auto        context = gentest::get_current_context();
    std::thread worker([context] {
        { auto adoption = gentest::set_current_context(context); }
        gentest::expect(true);
    });
    worker.join();
}

void conditional_thread_context() {
    auto        context = gentest::get_current_context();
    std::thread worker([context] {
        if (context) {
            auto adoption = gentest::set_current_context(context);
        }
        EXPECT_EQ(1, 1);
    });
    worker.join();
}

void control_init_scope_thread_context() {
    auto        context = gentest::get_current_context();
    std::thread worker([context] {
        if (auto adoption = gentest::set_current_context(context); context) {
            (void)adoption;
        }
        EXPECT_TRUE(true);
    });
    worker.join();
}

auto missing_coroutine_context() -> DetachedTask {
    co_await std::suspend_always{};
    gentest::require(true);
    co_return;
}

} // namespace
