#include "gentest/async.h"

#include <chrono>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

auto yield_forever() -> gentest::async_test<void> {
    while (true) {
        co_await gentest::async::yield();
    }
}

auto timeout_yielding_child() -> gentest::async_test<void> {
    auto result = co_await gentest::async::wait_for(yield_forever(), std::chrono::milliseconds(5));
    if (!result.timed_out()) {
        throw std::runtime_error("blocking scheduler allowed yielding child to beat expired timeout");
    }
}

auto fail(std::string_view message) -> int {
    std::cerr << message << '\n';
    return 1;
}

} // namespace

int main() try {
    std::string error;
    if (!gentest::detail::run_async_task_blocking(timeout_yielding_child(), "blocking timer starvation", error)) {
        return fail(error);
    }
    return 0;
} catch (const std::exception &ex) { return fail(ex.what()); } catch (...) {
    return fail("unknown exception");
}
