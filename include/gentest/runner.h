#pragma once

#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace gentest {

class failure : public std::runtime_error {
public:
    explicit failure(std::string message)
        : std::runtime_error(std::move(message)) {}
};

inline void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw failure(std::string(message));
    }
}

inline void expect_eq(auto&& lhs, auto&& rhs, std::string_view message = {}) {
    if (!(lhs == rhs)) {
        std::string text;
        if (!message.empty()) {
            text.append(message);
            text.append(": ");
        }
        text.append("expected equality");
        throw failure(std::move(text));
    }
}

inline void expect_ne(auto&& lhs, auto&& rhs, std::string_view message = {}) {
    if (!(lhs != rhs)) {
        std::string text;
        if (!message.empty()) {
            text.append(message);
            text.append(": ");
        }
        text.append("expected inequality");
        throw failure(std::move(text));
    }
}

inline void fail(std::string message) {
    throw failure(std::move(message));
}

auto run_all_tests(int argc, char** argv) -> int;
auto run_all_tests(std::span<const char*> args) -> int;

} // namespace gentest

