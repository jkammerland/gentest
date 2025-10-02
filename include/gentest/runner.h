#pragma once

#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace gentest {

// Lightweight assertion and test-runner interfaces used by generated code.
//
// All helper functions throw `gentest::failure` on assertion failure. The
// generated test runner catches these and reports them as [ FAIL ] lines.
//
// `run_all_tests` is the unified entry point emitted by the generator. The
// generator invokes it by name (configurable via `--entry`). It consumes the
// standard command-line arguments (or their span variant) and supports at
// least:
//   --list                 List discovered cases and their metadata
//   --shuffle-fixtures     Shuffle order within each fixture group only
//   --seed N               Seed used to initialize internal RNG for shuffling

class failure : public std::runtime_error {
  public:
    explicit failure(std::string message) : std::runtime_error(std::move(message)) {}
};

// Assert that `condition` is true, otherwise throws gentest::failure with `message`.
inline void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw failure(std::string(message));
    }
}

// Assert that `lhs == rhs` holds. Optional `message` is prefixed to the error text.
inline void expect_eq(auto &&lhs, auto &&rhs, std::string_view message = {}) {
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

// Assert that `lhs != rhs` holds. Optional `message` is prefixed to the error text.
inline void expect_ne(auto &&lhs, auto &&rhs, std::string_view message = {}) {
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

// Unconditionally throw a gentest::failure with the provided message.
inline void fail(std::string message) { throw failure(std::move(message)); }

// Unified test entry (argc/argv version). Consumed by generated code.
auto run_all_tests(int argc, char **argv) -> int;
// Unified test entry (span version). Consumed by generated code.
auto run_all_tests(std::span<const char *> args) -> int;

} // namespace gentest
