#pragma once

#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <source_location>
#include <mutex>
#include <memory>
#include <atomic>
#include <cstdio>
#include <cstdlib>

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

// Fatal assertion exception that is NOT derived from std::exception.
// This is intentionally separate from `failure` so we fully control the
// exception boundary. Destructors still run during stack unwinding.
class assertion {
  public:
    explicit assertion(std::string message) : msg_(std::move(message)) {}
    const std::string &message() const { return msg_; }

  private:
    std::string msg_;
};

namespace detail {
struct TestContextInfo {
    std::string              display_name;
    std::vector<std::string> failures;
    std::mutex               mtx;
    std::atomic<bool>        active{false};
};

inline thread_local std::shared_ptr<TestContextInfo> g_current_test{};

inline void set_current_test(std::shared_ptr<TestContextInfo> ctx) { g_current_test = std::move(ctx); }
inline std::shared_ptr<TestContextInfo> current_test() { return g_current_test; }
inline void record_failure(std::string msg) {
    auto ctx = g_current_test;
    if (!ctx || !ctx->active.load(std::memory_order_relaxed)) {
        std::fputs("gentest: fatal: assertion/expectation recorded without an active test context.\n"
                   "        Did you forget to adopt the test context in this thread/coroutine?\n", stderr);
        std::abort();
    }
    std::lock_guard<std::mutex> lk(ctx->mtx);
    ctx->failures.push_back(std::move(msg));
}
inline std::string loc_to_string(const std::source_location& loc) {
    std::string out(loc.file_name());
    out.push_back(':');
    out.append(std::to_string(loc.line()));
    out.append(" (in ");
    out.append(loc.function_name());
    out.push_back(')');
    return out;
}
} // namespace detail

// Public context adoption API for multi-threaded/coroutine tests.
namespace ctx {
using Token = std::shared_ptr<detail::TestContextInfo>;
inline Token current() { return detail::g_current_test; }
struct Adopt {
    Token prev;
    explicit Adopt(const Token& t) : prev(detail::g_current_test) { detail::g_current_test = t; }
    ~Adopt() { detail::g_current_test = prev; }
};
} // namespace ctx

// Assert that `condition` is true, otherwise throws gentest::failure with `message`.
inline void expect(bool condition, std::string_view message = {}, const std::source_location& loc = std::source_location::current()) {
    if (!condition) {
        std::string text;
        if (!message.empty()) { text.append(message); text.append(" :: "); }
        text.append("EXPECT_TRUE failed at ");
        text.append(::gentest::detail::loc_to_string(loc));
        ::gentest::detail::record_failure(std::move(text));
    }
}

// Assert that `lhs == rhs` holds. Optional `message` is prefixed to the error text.
inline void expect_eq(auto &&lhs, auto &&rhs, std::string_view message = {}, const std::source_location& loc = std::source_location::current()) {
    if (!(lhs == rhs)) {
        std::string text;
        if (!message.empty()) { text.append(message); text.append(" :: "); }
        text.append("EXPECT_EQ failed at ");
        text.append(::gentest::detail::loc_to_string(loc));
        ::gentest::detail::record_failure(std::move(text));
    }
}

// Assert that `lhs != rhs` holds. Optional `message` is prefixed to the error text.
inline void expect_ne(auto &&lhs, auto &&rhs, std::string_view message = {}, const std::source_location& loc = std::source_location::current()) {
    if (!(lhs != rhs)) {
        std::string text;
        if (!message.empty()) { text.append(message); text.append(" :: "); }
        text.append("EXPECT_NE failed at ");
        text.append(::gentest::detail::loc_to_string(loc));
        ::gentest::detail::record_failure(std::move(text));
    }
}

// Require that `condition` holds; throws gentest::assertion on failure.
inline void require(bool condition, std::string_view message = {}, const std::source_location& loc = std::source_location::current()) {
    if (!condition) {
        std::string text;
        if (!message.empty()) { text.append(message); text.append(" :: "); }
        text.append("ASSERT_TRUE failed at ");
        text.append(::gentest::detail::loc_to_string(loc));
        ::gentest::detail::record_failure(text);
        throw assertion("ASSERT_TRUE");
    }
}

// Require equality; throws gentest::assertion on mismatch.
inline void require_eq(auto &&lhs, auto &&rhs, std::string_view message = {}, const std::source_location& loc = std::source_location::current()) {
    if (!(lhs == rhs)) {
        std::string text;
        if (!message.empty()) { text.append(message); text.append(" :: "); }
        text.append("ASSERT_EQ failed at ");
        text.append(::gentest::detail::loc_to_string(loc));
        ::gentest::detail::record_failure(text);
        throw assertion("ASSERT_EQ");
    }
}

// Require inequality; throws gentest::assertion on mismatch.
inline void require_ne(auto &&lhs, auto &&rhs, std::string_view message = {}, const std::source_location& loc = std::source_location::current()) {
    if (!(lhs != rhs)) {
        std::string text;
        if (!message.empty()) { text.append(message); text.append(" :: "); }
        text.append("ASSERT_NE failed at ");
        text.append(::gentest::detail::loc_to_string(loc));
        ::gentest::detail::record_failure(text);
        throw assertion("ASSERT_NE");
    }
}

// Optional: alias-like helpers to align with requested naming without macro pitfalls.
// Prefer `require`/`require_eq` in portable code; `assert_true`/`assert_eq` are synonyms.
inline void assert_true(bool condition, std::string_view message) { require(condition, message); }
inline void assert_eq(auto &&lhs, auto &&rhs, std::string_view message = {}) { require_eq(std::forward<decltype(lhs)>(lhs), std::forward<decltype(rhs)>(rhs), message); }

// Uppercase assertion-style APIs (gtest-like) as inline functions (no macros).
// These live under gentest::asserts; test files may `using namespace gentest::asserts;`.
namespace asserts {
inline void EXPECT_TRUE(bool condition, std::string_view message = {}) { expect(condition, message); }
template <class L, class R>
inline void EXPECT_EQ(L &&lhs, R &&rhs, std::string_view message = {}) { expect_eq(std::forward<L>(lhs), std::forward<R>(rhs), message); }
template <class L, class R>
inline void EXPECT_NE(L &&lhs, R &&rhs, std::string_view message = {}) { expect_ne(std::forward<L>(lhs), std::forward<R>(rhs), message); }

inline void ASSERT_TRUE(bool condition, std::string_view message = {}) { require(condition, message); }
template <class L, class R>
inline void ASSERT_EQ(L &&lhs, R &&rhs, std::string_view message = {}) { require_eq(std::forward<L>(lhs), std::forward<R>(rhs), message); }
template <class L, class R>
inline void ASSERT_NE(L &&lhs, R &&rhs, std::string_view message = {}) { require_ne(std::forward<L>(lhs), std::forward<R>(rhs), message); }
} // namespace asserts

// Unconditionally throw a gentest::failure with the provided message.
inline void fail(std::string message) { throw failure(std::move(message)); }

// Unified test entry (argc/argv version). Consumed by generated code.
auto run_all_tests(int argc, char **argv) -> int;
// Unified test entry (span version). Consumed by generated code.
auto run_all_tests(std::span<const char *> args) -> int;

} // namespace gentest
