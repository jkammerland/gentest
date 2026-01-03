#pragma once

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <exception>
#include <mutex>
#include <ostream>
#include <source_location>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeinfo>
#include <vector>
#include <filesystem>

namespace gentest {

// Lightweight assertion and test-runner interfaces used by generated code.
//
// Assertions fall into two categories:
// - `expect*`: record a non-fatal failure in the current test context and
//   continue executing the test.
// - `require*`: record a failure and abort the current test by throwing
//   `gentest::assertion` (not derived from std::exception). When exceptions are
//   disabled, `require*` terminates the process via `std::terminate()`.
//
// `run_all_tests` is the unified entry point emitted by the generator. The
// generator invokes it by name (configurable via `--entry`). It consumes the
// standard command-line arguments (or their span variant) and supports at
// least:
//   --list                 List discovered cases and their metadata
//   --shuffle             Shuffle tests (respects fixture/grouping). Order within
//                        each group is shuffled; groups remain contiguous.
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
    // Chronological event stream for console/JUnit (kind: 'F' failure, 'L' log)
    std::vector<std::string> event_lines;
    std::vector<char>        event_kinds;
    std::mutex               mtx;
    std::atomic<bool>        active{false};
    bool                     dump_logs_on_failure{false};

    bool        runtime_skip_requested{false};
    std::string runtime_skip_reason;

    bool        xfail_requested{false};
    std::string xfail_reason;
};

inline thread_local std::shared_ptr<TestContextInfo> g_current_test{};

struct skip_exception {};

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
    ctx->failures.push_back(std::move(msg));
    ctx->failure_locations.push_back({std::string{}, 0});
    // Record event (after pushing so that failures vector is up-to-date)
    ctx->event_lines.push_back(ctx->failures.back());
    ctx->event_kinds.push_back('F');
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
    // Record event (after pushing so that failures vector is up-to-date)
    ctx->event_lines.push_back(ctx->failures.back());
    ctx->event_kinds.push_back('F');
}
inline void append_label(std::string& out, std::string_view label) {
    static constexpr std::size_t kWidth = 11; // longest of EXPECT_TRUE/ASSERT_TRUE
    out.append(label);
    if (label.size() < kWidth) out.append(kWidth - label.size(), ' ');
    out.append(" failed at ");
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

template <typename T>
concept Ostreamable = requires(std::ostream &os, const T &v) {
    os << v;
};

template <typename T>
inline std::string to_string_fallback(const T &v) {
    if constexpr (Ostreamable<T>) {
        std::ostringstream oss;
        oss << std::boolalpha << v;
        return oss.str();
    } else {
#if defined(__clang__)
#if __has_feature(cxx_rtti)
        std::string out = typeid(T).name();
        out.append(" (unprintable)");
        return out;
#else
        return "(unprintable, enable RTTI)";
#endif
#elif defined(__GXX_RTTI) || defined(_CPPRTTI)
        std::string out = typeid(T).name();
        out.append(" (unprintable)");
        return out;
#else
        return "(unprintable, enable RTTI)";
#endif
    }
}

inline void append_message(std::string &out, std::string_view message) {
    if (message.empty())
        return;
    out.append(": ");
    out.append(message);
}

template <typename L, typename R>
inline void append_cmp_values(std::string &out, const L &lhs, const R &rhs, std::string_view message) {
    out.append(message.empty() ? ": " : "; ");
    out.append("lhs=");
    out.append(to_string_fallback(lhs));
    out.append(", rhs=");
    out.append(to_string_fallback(rhs));
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
    if (ctx->dump_logs_on_failure) {
        ctx->event_lines.emplace_back(message);
        ctx->event_kinds.push_back('L');
    }
}
inline void log_on_fail(bool enable = true) {
    auto ctx = detail::g_current_test;
    if (!ctx || !ctx->active.load(std::memory_order_relaxed))
        return;
    std::lock_guard<std::mutex> lk(ctx->mtx);
    ctx->dump_logs_on_failure = enable;
}
inline void clear_logs() {
    auto ctx = detail::g_current_test;
    if (!ctx || !ctx->active.load(std::memory_order_relaxed))
        return;
    std::lock_guard<std::mutex> lk(ctx->mtx);
    ctx->logs.clear();
    // Remove any pending log events; keep failure events
    const std::size_t n = ctx->event_lines.size() < ctx->event_kinds.size() ? ctx->event_lines.size() : ctx->event_kinds.size();
    if (ctx->event_lines.size() != ctx->event_kinds.size()) {
        ctx->event_lines.resize(n);
        ctx->event_kinds.resize(n);
    }
    if (n != 0) {
        std::vector<std::string> kept_lines;
        std::vector<char>        kept_kinds;
        kept_lines.reserve(n);
        kept_kinds.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            if (ctx->event_kinds[i] == 'F') { kept_lines.push_back(std::move(ctx->event_lines[i])); kept_kinds.push_back('F'); }
        }
        ctx->event_lines.swap(kept_lines);
        ctx->event_kinds.swap(kept_kinds);
    }
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
            const long double abs_a = a < 0 ? -a : a;
            const long double abs_t = target < 0 ? -target : target;
            const long double scale = abs_a > abs_t ? abs_a : abs_t;
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

// Record a non-fatal failure if `condition` is false; execution continues.
inline void expect(bool condition, std::string_view message = {}, const std::source_location &loc = std::source_location::current()) {
    if (!condition) {
        std::string text;
        ::gentest::detail::append_label(text, "EXPECT_TRUE");
        text.append(::gentest::detail::loc_to_string(loc));
        ::gentest::detail::append_message(text, message);
        ::gentest::detail::record_failure(std::move(text), loc);
    }
}

// Record a non-fatal failure if `lhs == rhs` does not hold; execution continues.
inline void expect_eq(auto &&lhs, auto &&rhs, std::string_view message = {},
                      const std::source_location &loc = std::source_location::current()) {
    if (!(lhs == rhs)) {
        std::string text;
        ::gentest::detail::append_label(text, "EXPECT_EQ");
        text.append(::gentest::detail::loc_to_string(loc));
        ::gentest::detail::append_message(text, message);
        ::gentest::detail::append_cmp_values(text, lhs, rhs, message);
        ::gentest::detail::record_failure(std::move(text), loc);
    }
}

// Record a non-fatal failure if `lhs != rhs` does not hold; execution continues.
inline void expect_ne(auto &&lhs, auto &&rhs, std::string_view message = {},
                      const std::source_location &loc = std::source_location::current()) {
    if (!(lhs != rhs)) {
        std::string text;
        ::gentest::detail::append_label(text, "EXPECT_NE");
        text.append(::gentest::detail::loc_to_string(loc));
        ::gentest::detail::append_message(text, message);
        ::gentest::detail::append_cmp_values(text, lhs, rhs, message);
        ::gentest::detail::record_failure(std::move(text), loc);
    }
}

// Record a failure if `condition` is false and abort the current test.
// - Exceptions enabled: throws `gentest::assertion`
// - Exceptions disabled: terminates via `std::terminate()`
inline void require(bool condition, std::string_view message = {}, const std::source_location &loc = std::source_location::current()) {
    if (!condition) {
        std::string text;
        ::gentest::detail::append_label(text, "ASSERT_TRUE");
        text.append(::gentest::detail::loc_to_string(loc));
        ::gentest::detail::append_message(text, message);
        ::gentest::detail::record_failure(std::move(text), loc);
#if GENTEST_EXCEPTIONS_ENABLED
        throw assertion("ASSERT_TRUE");
#else
        ::gentest::detail::terminate_no_exceptions_fatal("gentest::require");
#endif
    }
}

// Record a failure if `lhs == rhs` does not hold and abort the current test.
// - Exceptions enabled: throws `gentest::assertion`
// - Exceptions disabled: terminates via `std::terminate()`
inline void require_eq(auto &&lhs, auto &&rhs, std::string_view message = {},
                       const std::source_location &loc = std::source_location::current()) {
    if (!(lhs == rhs)) {
        std::string text;
        ::gentest::detail::append_label(text, "ASSERT_EQ");
        text.append(::gentest::detail::loc_to_string(loc));
        ::gentest::detail::append_message(text, message);
        ::gentest::detail::append_cmp_values(text, lhs, rhs, message);
        ::gentest::detail::record_failure(std::move(text), loc);
#if GENTEST_EXCEPTIONS_ENABLED
        throw assertion("ASSERT_EQ");
#else
        ::gentest::detail::terminate_no_exceptions_fatal("gentest::require_eq");
#endif
    }
}

// Record a failure if `lhs != rhs` does not hold and abort the current test.
// - Exceptions enabled: throws `gentest::assertion`
// - Exceptions disabled: terminates via `std::terminate()`
inline void require_ne(auto &&lhs, auto &&rhs, std::string_view message = {},
                       const std::source_location &loc = std::source_location::current()) {
    if (!(lhs != rhs)) {
        std::string text;
        ::gentest::detail::append_label(text, "ASSERT_NE");
        text.append(::gentest::detail::loc_to_string(loc));
        ::gentest::detail::append_message(text, message);
        ::gentest::detail::append_cmp_values(text, lhs, rhs, message);
        ::gentest::detail::record_failure(std::move(text), loc);
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

[[noreturn]] inline void skip(std::string_view reason = {}, const std::source_location& loc = std::source_location::current()) {
    (void)loc;
    auto ctx = detail::g_current_test;
    if (!ctx || !ctx->active.load(std::memory_order_relaxed)) {
        std::fputs("gentest: fatal: skip called without an active test context.\n"
                   "        Did you forget to adopt the test context in this thread/coroutine?\n",
                   stderr);
        std::abort();
    }
    {
        std::lock_guard<std::mutex> lk(ctx->mtx);
        ctx->runtime_skip_requested = true;
        ctx->runtime_skip_reason    = std::string(reason);
    }
#if GENTEST_EXCEPTIONS_ENABLED
    throw detail::skip_exception{};
#else
    ::gentest::detail::terminate_no_exceptions_fatal("gentest::skip");
#endif
}

inline void skip_if(bool condition, std::string_view reason = {}, const std::source_location& loc = std::source_location::current()) {
    if (condition) skip(reason, loc);
}

inline void xfail(std::string_view reason = {}, const std::source_location& loc = std::source_location::current()) {
    (void)loc;
    auto ctx = detail::g_current_test;
    if (!ctx || !ctx->active.load(std::memory_order_relaxed)) {
        std::fputs("gentest: fatal: xfail called without an active test context.\n"
                   "        Did you forget to adopt the test context in this thread/coroutine?\n",
                   stderr);
        std::abort();
    }
    std::lock_guard<std::mutex> lk(ctx->mtx);
    ctx->xfail_requested = true;
    if (!reason.empty()) ctx->xfail_reason = std::string(reason);
}

inline void xfail_if(bool condition, std::string_view reason = {}, const std::source_location& loc = std::source_location::current()) {
    if (condition) xfail(reason, loc);
}

// Unified test entry (argc/argv version). Consumed by generated code.
auto run_all_tests(int argc, char **argv) -> int;
// Unified test entry (span version). Consumed by generated code.
auto run_all_tests(std::span<const char *> args) -> int;

// Runtime-visible test case description used by the generated manifest.
// The generator produces a constexpr array of Case entries and provides access
// via gentest::get_cases()/gentest::get_case_count() defined in the generated TU.
enum class FixtureLifetime {
    None,
    MemberEphemeral,
    MemberSuite,
    MemberGlobal,
};

using FixtureAccessor = void* (*)(std::string_view);

struct Case {
    std::string_view                  name;
    void (*fn)(void*);
    std::string_view                  file;
    unsigned                          line;
    bool                              is_benchmark{false};
    bool                              is_jitter{false};
    bool                              is_baseline{false};
    std::span<const std::string_view> tags;
    std::span<const std::string_view> requirements;
    std::string_view                  skip_reason;
    bool                              should_skip;
    std::string_view                  fixture;        // empty for free tests
    FixtureLifetime                   fixture_lifetime;
    std::string_view                  suite;
    FixtureAccessor                   acquire_fixture;
};

// Provided by the generated manifest TU
const Case* get_cases();
std::size_t get_case_count();

} // namespace gentest
