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

void function_thread_entry_without_token() { gentest::log("missing function thread adoption"); }

void missing_thread_token() {
    header_tidy_token_adoption::missing_header_thread_token();

    std::thread worker([] {
        gentest::log("missing thread adoption");
        EXPECT_TRUE(true);
    });
    worker.join();
}

void missing_async_token() {
    auto worker = std::async(std::launch::async, [] { gentest::expect(true); });
    worker.wait();
}

void stored_lambda_thread_token() {
    auto        worker_body = [] { gentest::expect(true); };
    std::thread worker(worker_body);
    worker.join();
}

void function_thread_token() {
    std::thread worker(function_thread_entry_without_token);
    worker.join();
}

void discarded_thread_token() {
    auto        token = gentest::get_current_token();
    std::thread worker([token] {
        static_cast<void>(gentest::set_current_token(token));
        gentest::log("discarded adoption");
    });
    worker.join();
}

void expired_scope_thread_token() {
    auto        token = gentest::get_current_token();
    std::thread worker([token] {
        { auto adoption = gentest::set_current_token(token); }
        gentest::expect(true);
    });
    worker.join();
}

void conditional_thread_token() {
    auto        token = gentest::get_current_token();
    std::thread worker([token] {
        if (token) {
            auto adoption = gentest::set_current_token(token);
        }
        EXPECT_EQ(1, 1);
    });
    worker.join();
}

void control_init_scope_thread_token() {
    auto        token = gentest::get_current_token();
    std::thread worker([token] {
        if (auto adoption = gentest::set_current_token(token); token) {
            (void)adoption;
        }
        EXPECT_TRUE(true);
    });
    worker.join();
}

auto missing_coroutine_token() -> DetachedTask {
    co_await std::suspend_always{};
    gentest::require(true);
    co_return;
}

} // namespace
