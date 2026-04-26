#pragma once

#include "gentest/detail/runtime_base.h"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <fmt/format.h>
#include <iterator>
#include <ostream>
#include <source_location>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <utility>

namespace gentest::detail {

inline constexpr bool exceptions_enabled = GENTEST_EXCEPTIONS_ENABLED != 0;

struct skip_exception {};

enum class BenchPhase {
    None,
    Setup,
    Call,
    Teardown,
};

GENTEST_RUNTIME_API auto bench_phase_storage() -> BenchPhase &;
GENTEST_RUNTIME_API auto bench_error_storage() -> std::string &;

struct BenchPhaseScope {
    BenchPhase prev;
    explicit BenchPhaseScope(BenchPhase next) : prev(bench_phase_storage()) { bench_phase_storage() = next; }
    ~BenchPhaseScope() { bench_phase_storage() = prev; }
};

inline BenchPhase bench_phase() { return bench_phase_storage(); }

inline void record_bench_error(std::string msg) {
    auto &bench_error = bench_error_storage();
    if (bench_error.empty()) {
        bench_error = std::move(msg);
    }
}

inline void clear_bench_error() { bench_error_storage().clear(); }

inline bool has_bench_error() { return !bench_error_storage().empty(); }

inline std::string take_bench_error() {
    auto       &bench_error = bench_error_storage();
    std::string out         = std::move(bench_error);
    bench_error.clear();
    return out;
}

using NoExceptionsFatalHook = void (*)(void *) noexcept;

struct NoExceptionsFatalHookState {
    NoExceptionsFatalHook hook      = nullptr;
    void                 *user_data = nullptr;
};

GENTEST_RUNTIME_API auto noexceptions_fatal_hook_storage() -> NoExceptionsFatalHookState &;

struct NoExceptionsFatalHookScope {
    NoExceptionsFatalHookState previous{};

    explicit NoExceptionsFatalHookScope(NoExceptionsFatalHook hook, void *user_data) noexcept
        : previous(noexceptions_fatal_hook_storage()) {
        noexceptions_fatal_hook_storage() = {
            .hook      = hook,
            .user_data = user_data,
        };
    }

    NoExceptionsFatalHookScope(const NoExceptionsFatalHookScope &)            = delete;
    NoExceptionsFatalHookScope &operator=(const NoExceptionsFatalHookScope &) = delete;

    ~NoExceptionsFatalHookScope() { noexceptions_fatal_hook_storage() = previous; }
};

inline void run_noexceptions_fatal_hook() noexcept {
    const auto state = noexceptions_fatal_hook_storage();
    if (!state.hook) {
        return;
    }
    noexceptions_fatal_hook_storage() = {};
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
        return fmt::format("{} (unprintable)", typeid(T).name());
#else
        return "(unprintable, enable RTTI)";
#endif
#elif defined(__GXX_RTTI) || defined(_CPPRTTI)
        return fmt::format("{} (unprintable)", typeid(T).name());
#else
        return "(unprintable, enable RTTI)";
#endif
    }
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
    return fmt::format("{}:{}", s, loc.line());
}

inline std::string failure_site_text(std::string_view label, const std::source_location &loc) {
    return fmt::format("{:<12} failed at {}", label, loc_to_string(loc));
}

inline std::string failure_text(std::string_view label, const std::source_location &loc, std::string_view message = {}) {
    std::string out = failure_site_text(label, loc);
    if (!message.empty()) {
        fmt::format_to(std::back_inserter(out), ": {}", message);
    }
    return out;
}

template <typename... Args>
inline std::string format_failure_text(std::string_view label, const std::source_location &loc, fmt::format_string<Args...> format_string,
                                       Args &&...args) {
    std::string out = failure_site_text(label, loc);
    fmt::format_to(std::back_inserter(out), ": ");
    fmt::format_to(std::back_inserter(out), format_string, std::forward<Args>(args)...);
    return out;
}

template <typename L, typename R>
inline std::string comparison_failure_text(std::string_view label, const std::source_location &loc, std::string_view message, const L &lhs,
                                           const R &rhs) {
    std::string out = failure_text(label, loc, message);
    fmt::format_to(std::back_inserter(out), "{}lhs={}, rhs={}", message.empty() ? ": " : "; ", to_string_fallback(lhs),
                   to_string_fallback(rhs));
    return out;
}

GENTEST_RUNTIME_API void record_failure(std::string msg);
GENTEST_RUNTIME_API void record_failure(std::string msg, const std::source_location &loc);
GENTEST_RUNTIME_API void record_failure_at(std::string msg, std::string file, unsigned line);
GENTEST_RUNTIME_API void record_failure_detail(std::string msg);

[[noreturn]] inline void terminate_no_exceptions_fatal(std::string_view origin) {
    ::gentest::detail::run_noexceptions_fatal_hook();
    (void)std::fputs("gentest: exceptions are disabled; terminating after fatal assertion", stderr);
    if (!origin.empty()) {
        (void)std::fputs(" (origin: ", stderr);
        (void)std::fwrite(origin.data(), 1, origin.size(), stderr);
        (void)std::fputs(")", stderr);
    }
    (void)std::fputs(".\n", stderr);
    (void)std::fflush(stderr);
    std::terminate();
}

[[noreturn]] GENTEST_RUNTIME_API void skip_shared_fixture_unavailable(std::string_view            reason,
                                                                      const std::source_location &loc = std::source_location::current());

template <class Expected, class Fn> inline void expect_throw(Fn &&fn, std::string_view expected_name, const std::source_location &loc) {
#if !GENTEST_EXCEPTIONS_ENABLED
    (void)fn;
    ::gentest::detail::record_failure(
        ::gentest::detail::failure_text("EXPECT_THROW", loc, "exceptions are disabled; cannot verify thrown exception"), loc);
#else
    using ExpectedT = std::remove_cvref_t<Expected>;

    if constexpr (std::is_base_of_v<std::exception, ExpectedT>) {
        try {
            fn();
        } catch (const gentest::detail::skip_exception &) { throw; } catch (const gentest::failure &) {
            if constexpr (std::is_same_v<ExpectedT, gentest::failure>) {
                return;
            }
            throw;
        } catch (const gentest::assertion &) {
            if constexpr (std::is_same_v<ExpectedT, gentest::assertion>) {
                return;
            }
            throw;
        } catch (const std::exception &err) {
            if (dynamic_cast<const ExpectedT *>(&err) != nullptr) {
                return;
            }
            ::gentest::detail::record_failure(::gentest::detail::format_failure_text("EXPECT_THROW", loc,
                                                                                     "expected {} but caught std::exception: {}",
                                                                                     expected_name, err.what()),
                                              loc);
            return;
        } catch (...) {
            ::gentest::detail::record_failure(
                ::gentest::detail::format_failure_text("EXPECT_THROW", loc, "expected {} but caught unknown exception", expected_name),
                loc);
            return;
        }
    } else {
        try {
            fn();
        } catch (const gentest::detail::skip_exception &) { throw; } catch (const gentest::failure &) {
            if constexpr (std::is_same_v<ExpectedT, gentest::failure>) {
                return;
            }
            throw;
        } catch (const gentest::assertion &) {
            if constexpr (std::is_same_v<ExpectedT, gentest::assertion>) {
                return;
            }
            throw;
        } catch (const ExpectedT &) { return; } catch (const std::exception &err) {
            ::gentest::detail::record_failure(::gentest::detail::format_failure_text("EXPECT_THROW", loc,
                                                                                     "expected {} but caught std::exception: {}",
                                                                                     expected_name, err.what()),
                                              loc);
            return;
        } catch (...) {
            ::gentest::detail::record_failure(
                ::gentest::detail::format_failure_text("EXPECT_THROW", loc, "expected {} but caught unknown exception", expected_name),
                loc);
            return;
        }
    }

    ::gentest::detail::record_failure(
        ::gentest::detail::format_failure_text("EXPECT_THROW", loc, "expected {} but no exception was thrown", expected_name), loc);
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
        ::gentest::detail::record_failure(
            ::gentest::detail::format_failure_text("EXPECT_NO_THROW", loc, "caught std::exception: {}", err.what()), loc);
    } catch (...) {
        ::gentest::detail::record_failure(::gentest::detail::failure_text("EXPECT_NO_THROW", loc, "caught unknown exception"), loc);
    }
#endif
}

template <class Expected, class Fn> inline void require_throw(Fn &&fn, std::string_view expected_name, const std::source_location &loc) {
#if !GENTEST_EXCEPTIONS_ENABLED
    (void)fn;
    ::gentest::detail::record_failure(
        ::gentest::detail::failure_text("ASSERT_THROW", loc, "exceptions are disabled; cannot verify thrown exception"), loc);
    ::gentest::detail::terminate_no_exceptions_fatal("gentest::require_throw");
#else
    using ExpectedT = std::remove_cvref_t<Expected>;

    if constexpr (std::is_base_of_v<std::exception, ExpectedT>) {
        try {
            fn();
        } catch (const gentest::detail::skip_exception &) { throw; } catch (const gentest::failure &) {
            if constexpr (std::is_same_v<ExpectedT, gentest::failure>) {
                return;
            }
            throw;
        } catch (const gentest::assertion &) {
            if constexpr (std::is_same_v<ExpectedT, gentest::assertion>) {
                return;
            }
            throw;
        } catch (const std::exception &err) {
            if (dynamic_cast<const ExpectedT *>(&err) != nullptr) {
                return;
            }
            ::gentest::detail::record_failure(::gentest::detail::format_failure_text("ASSERT_THROW", loc,
                                                                                     "expected {} but caught std::exception: {}",
                                                                                     expected_name, err.what()),
                                              loc);
            throw gentest::assertion("ASSERT_THROW");
        } catch (...) {
            ::gentest::detail::record_failure(
                ::gentest::detail::format_failure_text("ASSERT_THROW", loc, "expected {} but caught unknown exception", expected_name),
                loc);
            throw gentest::assertion("ASSERT_THROW");
        }
    } else {
        try {
            fn();
        } catch (const gentest::detail::skip_exception &) { throw; } catch (const gentest::failure &) {
            if constexpr (std::is_same_v<ExpectedT, gentest::failure>) {
                return;
            }
            throw;
        } catch (const gentest::assertion &) {
            if constexpr (std::is_same_v<ExpectedT, gentest::assertion>) {
                return;
            }
            throw;
        } catch (const ExpectedT &) { return; } catch (const std::exception &err) {
            ::gentest::detail::record_failure(::gentest::detail::format_failure_text("ASSERT_THROW", loc,
                                                                                     "expected {} but caught std::exception: {}",
                                                                                     expected_name, err.what()),
                                              loc);
            throw gentest::assertion("ASSERT_THROW");
        } catch (...) {
            ::gentest::detail::record_failure(
                ::gentest::detail::format_failure_text("ASSERT_THROW", loc, "expected {} but caught unknown exception", expected_name),
                loc);
            throw gentest::assertion("ASSERT_THROW");
        }
    }

    ::gentest::detail::record_failure(
        ::gentest::detail::format_failure_text("ASSERT_THROW", loc, "expected {} but no exception was thrown", expected_name), loc);
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
        ::gentest::detail::record_failure(
            ::gentest::detail::format_failure_text("ASSERT_NO_THROW", loc, "caught std::exception: {}", err.what()), loc);
        throw gentest::assertion("ASSERT_NO_THROW");
    } catch (...) {
        ::gentest::detail::record_failure(::gentest::detail::failure_text("ASSERT_NO_THROW", loc, "caught unknown exception"), loc);
        throw gentest::assertion("ASSERT_NO_THROW");
    }
#endif
}

} // namespace gentest::detail
