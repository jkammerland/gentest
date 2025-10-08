#pragma once

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <filesystem>

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
    struct FailureLoc {
        std::string file;
        unsigned    line = 0;
    };
    std::vector<FailureLoc>  failure_locations;
    std::vector<std::string> logs;
    std::size_t              logs_emitted = 0; // number of log lines already attached to previous failures
    std::mutex               mtx;
    std::atomic<bool>        active{false};
    bool                     dump_logs_on_failure{false};
};

inline thread_local std::shared_ptr<TestContextInfo> g_current_test{};

inline void                             set_current_test(std::shared_ptr<TestContextInfo> ctx) { g_current_test = std::move(ctx); }
inline std::shared_ptr<TestContextInfo> current_test() { return g_current_test; }
inline void                             record_failure(std::string msg) {
    auto ctx = g_current_test;
    if (!ctx || !ctx->active.load(std::memory_order_relaxed)) {
        std::fputs("gentest: fatal: assertion/expectation recorded without an active test context.\n"
                                                                           "        Did you forget to adopt the test context in this thread/coroutine?\n",
                                               stderr);
        std::abort();
    }
    std::lock_guard<std::mutex> lk(ctx->mtx);
    if (ctx->dump_logs_on_failure && ctx->logs_emitted < ctx->logs.size()) {
        for (std::size_t i = ctx->logs_emitted; i < ctx->logs.size(); ++i) {
            msg.append("\n");
            msg.append(ctx->logs[i]);
        }
        ctx->logs_emitted = ctx->logs.size();
    }
    ctx->failures.push_back(std::move(msg));
    ctx->failure_locations.push_back({std::string{}, 0});
}
inline void record_failure(std::string msg, const std::source_location &loc) {
    auto ctx = g_current_test;
    if (!ctx || !ctx->active.load(std::memory_order_relaxed)) {
        std::fputs("gentest: fatal: assertion/expectation recorded without an active test context.\n"
                   "        Did you forget to adopt the test context in this thread/coroutine?\n",
                   stderr);
        std::abort();
    }
    std::lock_guard<std::mutex> lk(ctx->mtx);
    if (ctx->dump_logs_on_failure && ctx->logs_emitted < ctx->logs.size()) {
        for (std::size_t i = ctx->logs_emitted; i < ctx->logs.size(); ++i) {
            msg.append("\n");
            msg.append(ctx->logs[i]);
        }
        ctx->logs_emitted = ctx->logs.size();
    }
    ctx->failures.push_back(std::move(msg));
    // Normalize path to a stable, short form for diagnostics
    std::filesystem::path p(std::string(loc.file_name()));
    p = p.lexically_normal();
    std::string s = p.generic_string();
    auto keep_from = [&](std::string_view marker) -> bool {
        const std::size_t pos = s.find(marker);
        if (pos != std::string::npos) { s = s.substr(pos); return true; }
        return false;
    };
    (void)(keep_from("tests/") || keep_from("include/") || keep_from("src/") || keep_from("tools/"));
    ctx->failure_locations.push_back({std::move(s), loc.line()});
}
inline std::string loc_to_string(const std::source_location &loc) {
    std::filesystem::path p(std::string(loc.file_name()));
    p = p.lexically_normal();
    std::string s = p.generic_string();
    auto keep_from = [&](std::string_view marker) -> bool {
        const std::size_t pos = s.find(marker);
        if (pos != std::string::npos) { s = s.substr(pos); return true; }
        return false;
    };
    (void)(keep_from("tests/") || keep_from("include/") || keep_from("src/") || keep_from("tools/"));
    s.push_back(':');
    s.append(std::to_string(loc.line()));
    return s;
}

// Exception support detection
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#define GENTEST_EXCEPTIONS_ENABLED 1
#else
#define GENTEST_EXCEPTIONS_ENABLED 0
#endif

[[noreturn]] inline void terminate_no_exceptions_fatal(std::string_view origin) {
    std::fputs("gentest: exceptions are disabled; terminating after fatal assertion", stderr);
    if (!origin.empty()) {
        std::fputs(" (origin: ", stderr);
        std::fwrite(origin.data(), 1, origin.size(), stderr);
        std::fputs(")", stderr);
    }
    std::fputs(".\n", stderr);
    std::fflush(stderr);
    std::terminate();
}
} // namespace detail

// Public context adoption API for multi-threaded/coroutine tests.
namespace ctx {
using Token = std::shared_ptr<detail::TestContextInfo>;
inline Token current() { return detail::g_current_test; }
struct Adopt {
    Token prev;
    explicit Adopt(const Token &t) : prev(detail::g_current_test) { detail::g_current_test = t; }
    ~Adopt() { detail::g_current_test = prev; }
};
} // namespace ctx

// Lightweight per-test logging; appended to failure messages when enabled.
inline void log(std::string_view message) {
    auto ctx = detail::g_current_test;
    if (!ctx || !ctx->active.load(std::memory_order_relaxed))
        return;
    std::lock_guard<std::mutex> lk(ctx->mtx);
    ctx->logs.emplace_back(message);
}
inline void log_on_fail(bool enable = true) {
    auto ctx = detail::g_current_test;
    if (!ctx || !ctx->active.load(std::memory_order_relaxed))
        return;
    ctx->dump_logs_on_failure = enable;
}
inline void clear_logs() {
    auto ctx = detail::g_current_test;
    if (!ctx || !ctx->active.load(std::memory_order_relaxed))
        return;
    std::lock_guard<std::mutex> lk(ctx->mtx);
    ctx->logs.clear();
    ctx->logs_emitted = 0;
}

// Approximate equality helper usable with EXPECT_EQ/ASSERT_EQ via operator==.
// Example: EXPECT_EQ(3.1415, gentest::approx::Approx(3.14).abs(0.01));
//          EXPECT_EQ(gentest::approx::Approx(100).rel(2.0), 101.0);
namespace approx {
struct Approx {
    long double target;
    long double abs_epsilon{0.0L};
    long double rel_percent{0.0L}; // unit percent; 1.0 means 1%

    explicit Approx(long double v) : target(v) {}

    Approx &abs(long double e) {
        abs_epsilon = e < 0 ? -e : e;
        return *this;
    }
    Approx &rel(long double percent) {
        rel_percent = percent < 0 ? -percent : percent;
        return *this;
    }

    template <typename T> bool matches(const T &value) const {
        const long double a    = static_cast<long double>(value);
        const long double diff = a > target ? (a - target) : (target - a);
        if (abs_epsilon > 0 && diff <= abs_epsilon)
            return true;
        if (rel_percent > 0) {
            const long double scale = (a > target ? a : target);
            const long double tol   = scale * (rel_percent / 100.0L);
            if (diff <= tol)
                return true;
        }
        return false;
    }
};

template <typename T> inline bool operator==(const T &lhs, const Approx &rhs) { return rhs.matches(lhs); }
template <typename T> inline bool operator==(const Approx &lhs, const T &rhs) { return lhs.matches(rhs); }
template <typename T> inline bool operator!=(const T &lhs, const Approx &rhs) { return !(lhs == rhs); }
template <typename T> inline bool operator!=(const Approx &lhs, const T &rhs) { return !(lhs == rhs); }
} // namespace approx

// Assert that `condition` is true, otherwise throws gentest::failure with `message`.
inline void expect(bool condition, std::string_view /*message*/ = {}, const std::source_location &loc = std::source_location::current()) {
    if (!condition) {
        std::string text;
        text.append("EXPECT_TRUE failed at ");
        text.append(::gentest::detail::loc_to_string(loc));
        ::gentest::detail::record_failure(std::move(text), loc);
    }
}

// Assert that `lhs == rhs` holds. Optional `message` is prefixed to the error text.
inline void expect_eq(auto &&lhs, auto &&rhs, std::string_view /*message*/ = {},
                      const std::source_location &loc = std::source_location::current()) {
    if (!(lhs == rhs)) {
        std::string text;
        text.append("EXPECT_EQ failed at ");
        text.append(::gentest::detail::loc_to_string(loc));
        ::gentest::detail::record_failure(std::move(text), loc);
    }
}

// Assert that `lhs != rhs` holds. Optional `message` is prefixed to the error text.
inline void expect_ne(auto &&lhs, auto &&rhs, std::string_view /*message*/ = {},
                      const std::source_location &loc = std::source_location::current()) {
    if (!(lhs != rhs)) {
        std::string text;
        text.append("EXPECT_NE failed at ");
        text.append(::gentest::detail::loc_to_string(loc));
        ::gentest::detail::record_failure(std::move(text), loc);
    }
}

// Require that `condition` holds; throws gentest::assertion on failure.
inline void require(bool condition, std::string_view /*message*/ = {}, const std::source_location &loc = std::source_location::current()) {
    if (!condition) {
        std::string text;
        text.append("ASSERT_TRUE failed at ");
        text.append(::gentest::detail::loc_to_string(loc));
        ::gentest::detail::record_failure(text, loc);
#if GENTEST_EXCEPTIONS_ENABLED
        throw assertion("ASSERT_TRUE");
#else
        ::gentest::detail::terminate_no_exceptions_fatal("gentest::require");
#endif
    }
}

// Require equality; throws gentest::assertion on mismatch.
inline void require_eq(auto &&lhs, auto &&rhs, std::string_view /*message*/ = {},
                       const std::source_location &loc = std::source_location::current()) {
    if (!(lhs == rhs)) {
        std::string text;
        text.append("ASSERT_EQ failed at ");
        text.append(::gentest::detail::loc_to_string(loc));
        ::gentest::detail::record_failure(text, loc);
#if GENTEST_EXCEPTIONS_ENABLED
        throw assertion("ASSERT_EQ");
#else
        ::gentest::detail::terminate_no_exceptions_fatal("gentest::require_eq");
#endif
    }
}

// Require inequality; throws gentest::assertion on mismatch.
inline void require_ne(auto &&lhs, auto &&rhs, std::string_view /*message*/ = {},
                       const std::source_location &loc = std::source_location::current()) {
    if (!(lhs != rhs)) {
        std::string text;
        text.append("ASSERT_NE failed at ");
        text.append(::gentest::detail::loc_to_string(loc));
        ::gentest::detail::record_failure(text, loc);
#if GENTEST_EXCEPTIONS_ENABLED
        throw assertion("ASSERT_NE");
#else
        ::gentest::detail::terminate_no_exceptions_fatal("gentest::require_ne");
#endif
    }
}

// Optional: alias-like helpers to align with requested naming without macro pitfalls.
// Prefer `require`/`require_eq` in portable code; `assert_true`/`assert_eq` are synonyms.
inline void assert_true(bool condition, std::string_view message,
                        const std::source_location &loc = std::source_location::current()) {
    require(condition, message, loc);
}
inline void assert_eq(auto &&lhs, auto &&rhs, std::string_view message = {},
                      const std::source_location &loc = std::source_location::current()) {
    require_eq(std::forward<decltype(lhs)>(lhs), std::forward<decltype(rhs)>(rhs), message, loc);
}

// Uppercase assertion-style APIs (gtest-like) as inline functions (no macros).
// These live under gentest::asserts; test files may `using namespace gentest::asserts;`.
namespace asserts {
inline void EXPECT_TRUE(bool condition, std::string_view message = {},
                        const std::source_location &loc = std::source_location::current()) {
    expect(condition, message, loc);
}
template <class L, class R>
inline void EXPECT_EQ(L &&lhs, R &&rhs, std::string_view message = {},
                      const std::source_location &loc = std::source_location::current()) {
    expect_eq(std::forward<L>(lhs), std::forward<R>(rhs), message, loc);
}
template <class L, class R>
inline void EXPECT_NE(L &&lhs, R &&rhs, std::string_view message = {},
                      const std::source_location &loc = std::source_location::current()) {
    expect_ne(std::forward<L>(lhs), std::forward<R>(rhs), message, loc);
}

inline void ASSERT_TRUE(bool condition, std::string_view message = {},
                        const std::source_location &loc = std::source_location::current()) {
    require(condition, message, loc);
}
template <class L, class R>
inline void ASSERT_EQ(L &&lhs, R &&rhs, std::string_view message = {},
                      const std::source_location &loc = std::source_location::current()) {
    require_eq(std::forward<L>(lhs), std::forward<R>(rhs), message, loc);
}
template <class L, class R>
inline void ASSERT_NE(L &&lhs, R &&rhs, std::string_view message = {},
                      const std::source_location &loc = std::source_location::current()) {
    require_ne(std::forward<L>(lhs), std::forward<R>(rhs), message, loc);
}
} // namespace asserts

// Unconditionally throw a gentest::failure with the provided message.
inline void fail(std::string message) { throw failure(std::move(message)); }

// Unified test entry (argc/argv version). Consumed by generated code.
auto run_all_tests(int argc, char **argv) -> int;
// Unified test entry (span version). Consumed by generated code.
auto run_all_tests(std::span<const char *> args) -> int;

} // namespace gentest
