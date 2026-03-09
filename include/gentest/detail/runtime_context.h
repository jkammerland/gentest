#pragma once

#include "gentest/detail/runtime_base.h"

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iterator>
#include <memory>
#include <mutex>
#include <ostream>
#include <source_location>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

namespace gentest::detail {

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
    std::mutex               adopted_mtx;
    std::condition_variable  adopted_cv;
    std::atomic<bool>        active{false};
    std::atomic<bool>        has_failures{false};
    std::atomic<std::size_t> adopted_tokens{0};
    bool                     dump_logs_on_failure{false};

    std::atomic<bool> runtime_skip_requested{false};
    std::string       runtime_skip_reason;
    enum class RuntimeSkipKind {
        User,
        SharedFixtureInfra,
    };
    RuntimeSkipKind runtime_skip_kind{RuntimeSkipKind::User};

    bool        xfail_requested{false};
    std::string xfail_reason;
};

struct TestContextLocalBuffer {
    TestContextInfo *owner = nullptr;

    std::vector<std::string>                 failures;
    std::vector<TestContextInfo::FailureLoc> failure_locations;
    std::vector<std::string>                 logs;
    std::vector<std::string>                 event_lines;
    std::vector<char>                        event_kinds;

    bool empty() const {
        return failures.empty() && failure_locations.empty() && logs.empty() && event_lines.empty() && event_kinds.empty();
    }

    void clear() {
        failures.clear();
        failure_locations.clear();
        logs.clear();
        event_lines.clear();
        event_kinds.clear();
    }
};

inline thread_local std::shared_ptr<TestContextInfo> g_current_test{};
inline thread_local TestContextLocalBuffer           g_current_buffer{};

struct skip_exception {};

enum class BenchPhase {
    None,
    Setup,
    Call,
    Teardown,
};
inline BenchPhase bench_phase();

inline void flush_current_buffer_for(TestContextInfo *ctx) {
    if (!ctx || g_current_buffer.owner != ctx || g_current_buffer.empty())
        return;

    std::lock_guard<std::mutex> lk(ctx->mtx);
    if (!g_current_buffer.failures.empty()) {
        ctx->failures.insert(ctx->failures.end(), std::make_move_iterator(g_current_buffer.failures.begin()),
                             std::make_move_iterator(g_current_buffer.failures.end()));
    }
    if (!g_current_buffer.failure_locations.empty()) {
        ctx->failure_locations.insert(ctx->failure_locations.end(), std::make_move_iterator(g_current_buffer.failure_locations.begin()),
                                      std::make_move_iterator(g_current_buffer.failure_locations.end()));
    }
    if (!g_current_buffer.logs.empty()) {
        ctx->logs.insert(ctx->logs.end(), std::make_move_iterator(g_current_buffer.logs.begin()),
                         std::make_move_iterator(g_current_buffer.logs.end()));
    }
    if (!g_current_buffer.event_lines.empty()) {
        ctx->event_lines.insert(ctx->event_lines.end(), std::make_move_iterator(g_current_buffer.event_lines.begin()),
                                std::make_move_iterator(g_current_buffer.event_lines.end()));
    }
    if (!g_current_buffer.event_kinds.empty()) {
        ctx->event_kinds.insert(ctx->event_kinds.end(), std::make_move_iterator(g_current_buffer.event_kinds.begin()),
                                std::make_move_iterator(g_current_buffer.event_kinds.end()));
    }
    g_current_buffer.clear();
}

inline void set_current_test(std::shared_ptr<TestContextInfo> ctx) {
    if (g_current_test) {
        flush_current_buffer_for(g_current_test.get());
    }
    g_current_test         = std::move(ctx);
    g_current_buffer.owner = g_current_test ? g_current_test.get() : nullptr;
}

inline std::shared_ptr<TestContextInfo> current_test() { return g_current_test; }

inline void wait_for_adopted_tokens(const std::shared_ptr<TestContextInfo> &ctx) {
    if (!ctx)
        return;
    // Intentional barrier: test/phase completion waits for all adopted contexts
    // to be released. If adopted work is leaked (for example detached/stuck),
    // completion blocks by design to preserve one-shot outcome accounting.
    std::unique_lock<std::mutex> lk(ctx->adopted_mtx);
    ctx->adopted_cv.wait(lk, [&] { return ctx->adopted_tokens.load(std::memory_order_acquire) == 0; });
}

inline void request_runtime_skip(std::string_view reason, TestContextInfo::RuntimeSkipKind kind) {
    auto ctx = g_current_test;
    if (!ctx || !ctx->active.load(std::memory_order_relaxed)) {
        std::fputs("gentest: fatal: skip called without an active test context.\n"
                   "        Did you forget to adopt the test context in this thread/coroutine?\n",
                   stderr);
        std::abort();
    }
    std::lock_guard<std::mutex> lk(ctx->mtx);
    ctx->runtime_skip_requested.store(true, std::memory_order_relaxed);
    ctx->runtime_skip_reason = std::string(reason);
    ctx->runtime_skip_kind   = kind;
}

inline void record_failure(std::string msg) {
    auto ctx = g_current_test;
    if (!ctx || !ctx->active.load(std::memory_order_relaxed)) {
        std::fputs("gentest: fatal: assertion/expectation recorded without an active test context.\n"
                   "        Did you forget to adopt the test context in this thread/coroutine?\n",
                   stderr);
        std::abort();
    }
    if (g_current_buffer.owner != ctx.get()) {
        flush_current_buffer_for(g_current_buffer.owner);
        g_current_buffer.owner = ctx.get();
    }
    g_current_buffer.failures.push_back(std::move(msg));
    ctx->has_failures.store(true, std::memory_order_relaxed);
    g_current_buffer.failure_locations.push_back({std::string{}, 0});
    g_current_buffer.event_lines.push_back(g_current_buffer.failures.back());
    g_current_buffer.event_kinds.push_back('F');
#if GENTEST_EXCEPTIONS_ENABLED
    if (bench_phase() == BenchPhase::Call) {
        throw gentest::assertion(g_current_buffer.failures.back());
    }
#endif
}

inline void record_failure(std::string msg, const std::source_location &loc) {
    auto ctx = g_current_test;
    if (!ctx || !ctx->active.load(std::memory_order_relaxed)) {
        std::fputs("gentest: fatal: assertion/expectation recorded without an active test context.\n"
                   "        Did you forget to adopt the test context in this thread/coroutine?\n",
                   stderr);
        std::abort();
    }
    if (g_current_buffer.owner != ctx.get()) {
        flush_current_buffer_for(g_current_buffer.owner);
        g_current_buffer.owner = ctx.get();
    }
    g_current_buffer.failures.push_back(std::move(msg));
    ctx->has_failures.store(true, std::memory_order_relaxed);
    // Normalize path to a stable, short form for diagnostics.
    std::filesystem::path p(std::string(loc.file_name()));
    p                     = p.lexically_normal();
    std::string s         = p.generic_string();
    auto        keep_from = [&](std::string_view marker) -> bool {
        const std::size_t pos = s.find(marker);
        if (pos != std::string::npos) {
            s = s.substr(pos);
            return true;
        }
        return false;
    };
    (void)(keep_from("tests/") || keep_from("include/") || keep_from("src/") || keep_from("tools/"));
    g_current_buffer.failure_locations.push_back({std::move(s), loc.line()});
    g_current_buffer.event_lines.push_back(g_current_buffer.failures.back());
    g_current_buffer.event_kinds.push_back('F');
#if GENTEST_EXCEPTIONS_ENABLED
    if (bench_phase() == BenchPhase::Call) {
        throw gentest::assertion(g_current_buffer.failures.back());
    }
#endif
}

inline void append_label(std::string &out, std::string_view label) {
    static constexpr std::size_t kWidth = 12; // longest of EXPECT_FALSE/ASSERT_FALSE
    out.append(label);
    if (label.size() < kWidth)
        out.append(kWidth - label.size(), ' ');
    out.append(" failed at ");
}

inline std::string loc_to_string(const std::source_location &loc) {
    std::filesystem::path p(std::string(loc.file_name()));
    p                     = p.lexically_normal();
    std::string s         = p.generic_string();
    auto        keep_from = [&](std::string_view marker) -> bool {
        const std::size_t pos = s.find(marker);
        if (pos != std::string::npos) {
            s = s.substr(pos);
            return true;
        }
        return false;
    };
    (void)(keep_from("tests/") || keep_from("include/") || keep_from("src/") || keep_from("tools/"));
    s.push_back(':');
    s.append(std::to_string(loc.line()));
    return s;
}

inline thread_local BenchPhase  g_bench_phase = BenchPhase::None;
inline thread_local std::string g_bench_error;

struct BenchPhaseScope {
    BenchPhase prev;
    explicit BenchPhaseScope(BenchPhase next) : prev(g_bench_phase) { g_bench_phase = next; }
    ~BenchPhaseScope() { g_bench_phase = prev; }
};

inline BenchPhase bench_phase() { return g_bench_phase; }

inline void record_bench_error(std::string msg) {
    if (g_bench_error.empty())
        g_bench_error = std::move(msg);
}

inline void clear_bench_error() { g_bench_error.clear(); }

inline bool has_bench_error() { return !g_bench_error.empty(); }

inline std::string take_bench_error() {
    std::string out = std::move(g_bench_error);
    g_bench_error.clear();
    return out;
}

using NoExceptionsFatalHook = void (*)(void *) noexcept;

struct NoExceptionsFatalHookState {
    NoExceptionsFatalHook hook      = nullptr;
    void                 *user_data = nullptr;
};

inline thread_local NoExceptionsFatalHookState g_noexceptions_fatal_hook{};

struct NoExceptionsFatalHookScope {
    NoExceptionsFatalHookState previous{};

    explicit NoExceptionsFatalHookScope(NoExceptionsFatalHook hook, void *user_data) noexcept : previous(g_noexceptions_fatal_hook) {
        g_noexceptions_fatal_hook = {
            .hook      = hook,
            .user_data = user_data,
        };
    }

    NoExceptionsFatalHookScope(const NoExceptionsFatalHookScope &)            = delete;
    NoExceptionsFatalHookScope &operator=(const NoExceptionsFatalHookScope &) = delete;

    ~NoExceptionsFatalHookScope() { g_noexceptions_fatal_hook = previous; }
};

inline void run_noexceptions_fatal_hook() noexcept {
    const auto state = g_noexceptions_fatal_hook;
    if (!state.hook) {
        return;
    }
    g_noexceptions_fatal_hook = {};
    state.hook(state.user_data);
}

template <typename T>
concept Ostreamable = requires(std::ostream &os, const T &v) { os << v; };

template <typename T> inline std::string to_string_fallback(const T &v) {
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

template <typename L, typename R> inline void append_cmp_values(std::string &out, const L &lhs, const R &rhs, std::string_view message) {
    out.append(message.empty() ? ": " : "; ");
    out.append("lhs=");
    out.append(to_string_fallback(lhs));
    out.append(", rhs=");
    out.append(to_string_fallback(rhs));
}

[[noreturn]] inline void terminate_no_exceptions_fatal(std::string_view origin) {
    ::gentest::detail::run_noexceptions_fatal_hook();
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

[[noreturn]] inline void skip_shared_fixture_unavailable(std::string_view            reason,
                                                         const std::source_location &loc = std::source_location::current()) {
    (void)loc;
    request_runtime_skip(reason, TestContextInfo::RuntimeSkipKind::SharedFixtureInfra);
#if GENTEST_EXCEPTIONS_ENABLED
    throw skip_exception{};
#else
    terminate_no_exceptions_fatal("gentest::detail::skip_shared_fixture_unavailable");
#endif
}

template <class Expected, class Fn> inline void expect_throw(Fn &&fn, std::string_view expected_name, const std::source_location &loc) {
#if !GENTEST_EXCEPTIONS_ENABLED
    (void)fn;
    std::string text;
    ::gentest::detail::append_label(text, "EXPECT_THROW");
    text.append(::gentest::detail::loc_to_string(loc));
    text.append(": exceptions are disabled; cannot verify thrown exception");
    ::gentest::detail::record_failure(std::move(text), loc);
#else
    try {
        fn();
    } catch (const gentest::detail::skip_exception &) { throw; } catch (const gentest::failure &) {
        // Preserve framework control flow: internal gentest failures are never
        // consumed by EXPECT_THROW when Expected is broader (for example std::exception).
        if constexpr (std::is_same_v<std::remove_cvref_t<Expected>, gentest::failure>) {
            return;
        }
        throw;
    } catch (const gentest::assertion &) {
        // Preserve framework control flow for fatal assertions as well.
        if constexpr (std::is_same_v<std::remove_cvref_t<Expected>, gentest::assertion>) {
            return;
        }
        throw;
    } catch (const Expected &) { return; } catch (const std::exception &err) {
        std::string text;
        ::gentest::detail::append_label(text, "EXPECT_THROW");
        text.append(::gentest::detail::loc_to_string(loc));
        text.append(": expected ");
        text.append(expected_name);
        text.append(" but caught std::exception: ");
        text.append(err.what());
        ::gentest::detail::record_failure(std::move(text), loc);
        return;
    } catch (...) {
        std::string text;
        ::gentest::detail::append_label(text, "EXPECT_THROW");
        text.append(::gentest::detail::loc_to_string(loc));
        text.append(": expected ");
        text.append(expected_name);
        text.append(" but caught unknown exception");
        ::gentest::detail::record_failure(std::move(text), loc);
        return;
    }

    std::string text;
    ::gentest::detail::append_label(text, "EXPECT_THROW");
    text.append(::gentest::detail::loc_to_string(loc));
    text.append(": expected ");
    text.append(expected_name);
    text.append(" but no exception was thrown");
    ::gentest::detail::record_failure(std::move(text), loc);
#endif
}

template <class Fn> inline void expect_no_throw(Fn &&fn, const std::source_location &loc) {
#if !GENTEST_EXCEPTIONS_ENABLED
    fn();
    (void)loc;
#else
    try {
        fn();
    } catch (const gentest::detail::skip_exception &) { throw; } catch (const gentest::failure &) {
        throw;
    } catch (const gentest::assertion &) { throw; } catch (const std::exception &err) {
        std::string text;
        ::gentest::detail::append_label(text, "EXPECT_NO_THROW");
        text.append(::gentest::detail::loc_to_string(loc));
        text.append(": caught std::exception: ");
        text.append(err.what());
        ::gentest::detail::record_failure(std::move(text), loc);
    } catch (...) {
        std::string text;
        ::gentest::detail::append_label(text, "EXPECT_NO_THROW");
        text.append(::gentest::detail::loc_to_string(loc));
        text.append(": caught unknown exception");
        ::gentest::detail::record_failure(std::move(text), loc);
    }
#endif
}

template <class Expected, class Fn> inline void require_throw(Fn &&fn, std::string_view expected_name, const std::source_location &loc) {
#if !GENTEST_EXCEPTIONS_ENABLED
    (void)fn;
    std::string text;
    ::gentest::detail::append_label(text, "ASSERT_THROW");
    text.append(::gentest::detail::loc_to_string(loc));
    text.append(": exceptions are disabled; cannot verify thrown exception");
    ::gentest::detail::record_failure(std::move(text), loc);
    ::gentest::detail::terminate_no_exceptions_fatal("gentest::require_throw");
#else
    try {
        fn();
    } catch (const gentest::detail::skip_exception &) { throw; } catch (const gentest::failure &) {
        // Preserve framework control flow: internal gentest failures are never
        // consumed by ASSERT_THROW when Expected is broader (for example std::exception).
        if constexpr (std::is_same_v<std::remove_cvref_t<Expected>, gentest::failure>) {
            return;
        }
        throw;
    } catch (const gentest::assertion &) {
        // Preserve framework control flow for fatal assertions as well.
        if constexpr (std::is_same_v<std::remove_cvref_t<Expected>, gentest::assertion>) {
            return;
        }
        throw;
    } catch (const Expected &) { return; } catch (const std::exception &err) {
        std::string text;
        ::gentest::detail::append_label(text, "ASSERT_THROW");
        text.append(::gentest::detail::loc_to_string(loc));
        text.append(": expected ");
        text.append(expected_name);
        text.append(" but caught std::exception: ");
        text.append(err.what());
        ::gentest::detail::record_failure(std::move(text), loc);
        throw gentest::assertion("ASSERT_THROW");
    } catch (...) {
        std::string text;
        ::gentest::detail::append_label(text, "ASSERT_THROW");
        text.append(::gentest::detail::loc_to_string(loc));
        text.append(": expected ");
        text.append(expected_name);
        text.append(" but caught unknown exception");
        ::gentest::detail::record_failure(std::move(text), loc);
        throw gentest::assertion("ASSERT_THROW");
    }

    std::string text;
    ::gentest::detail::append_label(text, "ASSERT_THROW");
    text.append(::gentest::detail::loc_to_string(loc));
    text.append(": expected ");
    text.append(expected_name);
    text.append(" but no exception was thrown");
    ::gentest::detail::record_failure(std::move(text), loc);
    throw gentest::assertion("ASSERT_THROW");
#endif
}

template <class Fn> inline void require_no_throw(Fn &&fn, const std::source_location &loc) {
#if !GENTEST_EXCEPTIONS_ENABLED
    fn();
    (void)loc;
#else
    try {
        fn();
    } catch (const gentest::detail::skip_exception &) { throw; } catch (const gentest::failure &) {
        throw;
    } catch (const gentest::assertion &) { throw; } catch (const std::exception &err) {
        std::string text;
        ::gentest::detail::append_label(text, "ASSERT_NO_THROW");
        text.append(::gentest::detail::loc_to_string(loc));
        text.append(": caught std::exception: ");
        text.append(err.what());
        ::gentest::detail::record_failure(std::move(text), loc);
        throw gentest::assertion("ASSERT_NO_THROW");
    } catch (...) {
        std::string text;
        ::gentest::detail::append_label(text, "ASSERT_NO_THROW");
        text.append(::gentest::detail::loc_to_string(loc));
        text.append(": caught unknown exception");
        ::gentest::detail::record_failure(std::move(text), loc);
        throw gentest::assertion("ASSERT_NO_THROW");
    }
#endif
}

} // namespace gentest::detail
