#pragma once

#include "gentest/detail/runtime_support.h"

#include <source_location>
#include <string>
#include <string_view>
#include <utility>

namespace gentest {

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
        const auto        a    = static_cast<long double>(value);
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
        ::gentest::detail::record_failure(::gentest::detail::failure_text("EXPECT_TRUE", loc, message), loc);
    }
}

// Record a non-fatal failure if `condition` is true; execution continues.
inline void expect_false(bool condition, std::string_view message = {}, const std::source_location &loc = std::source_location::current()) {
    if (condition) {
        ::gentest::detail::record_failure(::gentest::detail::failure_text("EXPECT_FALSE", loc, message), loc);
    }
}

// Record a non-fatal failure if `lhs == rhs` does not hold; execution continues.
inline void expect_eq(auto &&lhs, auto &&rhs, std::string_view message = {},
                      const std::source_location &loc = std::source_location::current()) {
    if (!(lhs == rhs)) {
        ::gentest::detail::record_failure(::gentest::detail::comparison_failure_text("EXPECT_EQ", loc, message, lhs, rhs), loc);
    }
}

// Record a non-fatal failure if `lhs != rhs` does not hold; execution continues.
inline void expect_ne(auto &&lhs, auto &&rhs, std::string_view message = {},
                      const std::source_location &loc = std::source_location::current()) {
    if (!(lhs != rhs)) {
        ::gentest::detail::record_failure(::gentest::detail::comparison_failure_text("EXPECT_NE", loc, message, lhs, rhs), loc);
    }
}

// Record a non-fatal failure if `lhs < rhs` does not hold; execution continues.
inline void expect_lt(auto &&lhs, auto &&rhs, std::string_view message = {},
                      const std::source_location &loc = std::source_location::current()) {
    if (!(lhs < rhs)) {
        ::gentest::detail::record_failure(::gentest::detail::comparison_failure_text("EXPECT_LT", loc, message, lhs, rhs), loc);
    }
}

// Record a non-fatal failure if `lhs <= rhs` does not hold; execution continues.
inline void expect_le(auto &&lhs, auto &&rhs, std::string_view message = {},
                      const std::source_location &loc = std::source_location::current()) {
    if (!(lhs <= rhs)) {
        ::gentest::detail::record_failure(::gentest::detail::comparison_failure_text("EXPECT_LE", loc, message, lhs, rhs), loc);
    }
}

// Record a non-fatal failure if `lhs > rhs` does not hold; execution continues.
inline void expect_gt(auto &&lhs, auto &&rhs, std::string_view message = {},
                      const std::source_location &loc = std::source_location::current()) {
    if (!(lhs > rhs)) {
        ::gentest::detail::record_failure(::gentest::detail::comparison_failure_text("EXPECT_GT", loc, message, lhs, rhs), loc);
    }
}

// Record a non-fatal failure if `lhs >= rhs` does not hold; execution continues.
inline void expect_ge(auto &&lhs, auto &&rhs, std::string_view message = {},
                      const std::source_location &loc = std::source_location::current()) {
    if (!(lhs >= rhs)) {
        ::gentest::detail::record_failure(::gentest::detail::comparison_failure_text("EXPECT_GE", loc, message, lhs, rhs), loc);
    }
}

// Record a failure if `condition` is false and abort the current test.
// - Exceptions enabled: throws `gentest::assertion`
// - Exceptions disabled: terminates via `std::terminate()`
inline void require(bool condition, std::string_view message = {}, const std::source_location &loc = std::source_location::current()) {
    if (!condition) {
        ::gentest::detail::record_failure(::gentest::detail::failure_text("ASSERT_TRUE", loc, message), loc);
#if GENTEST_EXCEPTIONS_ENABLED
        throw assertion("ASSERT_TRUE");
#else
        ::gentest::detail::terminate_no_exceptions_fatal("gentest::require");
#endif
    }
}

// Record a failure if `condition` is true and abort the current test.
// - Exceptions enabled: throws `gentest::assertion`
// - Exceptions disabled: terminates via `std::terminate()`
inline void require_false(bool condition, std::string_view message = {},
                          const std::source_location &loc = std::source_location::current()) {
    if (condition) {
        ::gentest::detail::record_failure(::gentest::detail::failure_text("ASSERT_FALSE", loc, message), loc);
#if GENTEST_EXCEPTIONS_ENABLED
        throw assertion("ASSERT_FALSE");
#else
        ::gentest::detail::terminate_no_exceptions_fatal("gentest::require_false");
#endif
    }
}

// Record a failure if `lhs == rhs` does not hold and abort the current test.
// - Exceptions enabled: throws `gentest::assertion`
// - Exceptions disabled: terminates via `std::terminate()`
inline void require_eq(auto &&lhs, auto &&rhs, std::string_view message = {},
                       const std::source_location &loc = std::source_location::current()) {
    if (!(lhs == rhs)) {
        ::gentest::detail::record_failure(::gentest::detail::comparison_failure_text("ASSERT_EQ", loc, message, lhs, rhs), loc);
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
        ::gentest::detail::record_failure(::gentest::detail::comparison_failure_text("ASSERT_NE", loc, message, lhs, rhs), loc);
#if GENTEST_EXCEPTIONS_ENABLED
        throw assertion("ASSERT_NE");
#else
        ::gentest::detail::terminate_no_exceptions_fatal("gentest::require_ne");
#endif
    }
}

// Record a failure if `lhs < rhs` does not hold and abort the current test.
// - Exceptions enabled: throws `gentest::assertion`
// - Exceptions disabled: terminates via `std::terminate()`
inline void require_lt(auto &&lhs, auto &&rhs, std::string_view message = {},
                       const std::source_location &loc = std::source_location::current()) {
    if (!(lhs < rhs)) {
        ::gentest::detail::record_failure(::gentest::detail::comparison_failure_text("ASSERT_LT", loc, message, lhs, rhs), loc);
#if GENTEST_EXCEPTIONS_ENABLED
        throw assertion("ASSERT_LT");
#else
        ::gentest::detail::terminate_no_exceptions_fatal("gentest::require_lt");
#endif
    }
}

// Record a failure if `lhs <= rhs` does not hold and abort the current test.
// - Exceptions enabled: throws `gentest::assertion`
// - Exceptions disabled: terminates via `std::terminate()`
inline void require_le(auto &&lhs, auto &&rhs, std::string_view message = {},
                       const std::source_location &loc = std::source_location::current()) {
    if (!(lhs <= rhs)) {
        ::gentest::detail::record_failure(::gentest::detail::comparison_failure_text("ASSERT_LE", loc, message, lhs, rhs), loc);
#if GENTEST_EXCEPTIONS_ENABLED
        throw assertion("ASSERT_LE");
#else
        ::gentest::detail::terminate_no_exceptions_fatal("gentest::require_le");
#endif
    }
}

// Record a failure if `lhs > rhs` does not hold and abort the current test.
// - Exceptions enabled: throws `gentest::assertion`
// - Exceptions disabled: terminates via `std::terminate()`
inline void require_gt(auto &&lhs, auto &&rhs, std::string_view message = {},
                       const std::source_location &loc = std::source_location::current()) {
    if (!(lhs > rhs)) {
        ::gentest::detail::record_failure(::gentest::detail::comparison_failure_text("ASSERT_GT", loc, message, lhs, rhs), loc);
#if GENTEST_EXCEPTIONS_ENABLED
        throw assertion("ASSERT_GT");
#else
        ::gentest::detail::terminate_no_exceptions_fatal("gentest::require_gt");
#endif
    }
}

// Record a failure if `lhs >= rhs` does not hold and abort the current test.
// - Exceptions enabled: throws `gentest::assertion`
// - Exceptions disabled: terminates via `std::terminate()`
inline void require_ge(auto &&lhs, auto &&rhs, std::string_view message = {},
                       const std::source_location &loc = std::source_location::current()) {
    if (!(lhs >= rhs)) {
        ::gentest::detail::record_failure(::gentest::detail::comparison_failure_text("ASSERT_GE", loc, message, lhs, rhs), loc);
#if GENTEST_EXCEPTIONS_ENABLED
        throw assertion("ASSERT_GE");
#else
        ::gentest::detail::terminate_no_exceptions_fatal("gentest::require_ge");
#endif
    }
}

// Optional: alias-like helpers to align with requested naming without macro pitfalls.
// Prefer `require`/`require_eq` in portable code; `assert_true`/`assert_eq` are synonyms.
inline void assert_true(bool condition, std::string_view message, const std::source_location &loc = std::source_location::current()) {
    require(condition, message, loc);
}

inline void assert_false(bool condition, std::string_view message, const std::source_location &loc = std::source_location::current()) {
    require_false(condition, message, loc);
}

inline void assert_eq(auto &&lhs, auto &&rhs, std::string_view message = {},
                      const std::source_location &loc = std::source_location::current()) {
    require_eq(std::forward<decltype(lhs)>(lhs), std::forward<decltype(rhs)>(rhs), message, loc);
}

// Uppercase assertion-style APIs (gtest-like) as inline functions (no macros).
// These live under gentest::asserts; test files may `using namespace gentest::asserts;`.
namespace asserts {

namespace detail_internal {

template <class Expected> inline std::string_view expected_exception_name() {
#if defined(__clang__)
#if __has_feature(cxx_rtti)
    return typeid(Expected).name();
#else
    return "exception";
#endif
#elif defined(__GXX_RTTI) || defined(_CPPRTTI)
    return typeid(Expected).name();
#else
    return "exception";
#endif
}

} // namespace detail_internal

inline void EXPECT_TRUE(bool condition, std::string_view message = {}, const std::source_location &loc = std::source_location::current()) {
    expect(condition, message, loc);
}

inline void EXPECT_FALSE(bool condition, std::string_view message = {}, const std::source_location &loc = std::source_location::current()) {
    expect_false(condition, message, loc);
}

template <class L, class R>
inline void EXPECT_EQ(L &&lhs, R &&rhs, std::string_view message = {}, const std::source_location &loc = std::source_location::current()) {
    expect_eq(std::forward<L>(lhs), std::forward<R>(rhs), message, loc);
}

template <class L, class R>
inline void EXPECT_NE(L &&lhs, R &&rhs, std::string_view message = {}, const std::source_location &loc = std::source_location::current()) {
    expect_ne(std::forward<L>(lhs), std::forward<R>(rhs), message, loc);
}

template <class L, class R>
inline void EXPECT_LT(L &&lhs, R &&rhs, std::string_view message = {}, const std::source_location &loc = std::source_location::current()) {
    expect_lt(std::forward<L>(lhs), std::forward<R>(rhs), message, loc);
}

template <class L, class R>
inline void EXPECT_LE(L &&lhs, R &&rhs, std::string_view message = {}, const std::source_location &loc = std::source_location::current()) {
    expect_le(std::forward<L>(lhs), std::forward<R>(rhs), message, loc);
}

template <class L, class R>
inline void EXPECT_GT(L &&lhs, R &&rhs, std::string_view message = {}, const std::source_location &loc = std::source_location::current()) {
    expect_gt(std::forward<L>(lhs), std::forward<R>(rhs), message, loc);
}

template <class L, class R>
inline void EXPECT_GE(L &&lhs, R &&rhs, std::string_view message = {}, const std::source_location &loc = std::source_location::current()) {
    expect_ge(std::forward<L>(lhs), std::forward<R>(rhs), message, loc);
}

inline void ASSERT_TRUE(bool condition, std::string_view message = {}, const std::source_location &loc = std::source_location::current()) {
    require(condition, message, loc);
}

inline void ASSERT_FALSE(bool condition, std::string_view message = {}, const std::source_location &loc = std::source_location::current()) {
    require_false(condition, message, loc);
}

template <class L, class R>
inline void ASSERT_EQ(L &&lhs, R &&rhs, std::string_view message = {}, const std::source_location &loc = std::source_location::current()) {
    require_eq(std::forward<L>(lhs), std::forward<R>(rhs), message, loc);
}

template <class L, class R>
inline void ASSERT_NE(L &&lhs, R &&rhs, std::string_view message = {}, const std::source_location &loc = std::source_location::current()) {
    require_ne(std::forward<L>(lhs), std::forward<R>(rhs), message, loc);
}

template <class L, class R>
inline void ASSERT_LT(L &&lhs, R &&rhs, std::string_view message = {}, const std::source_location &loc = std::source_location::current()) {
    require_lt(std::forward<L>(lhs), std::forward<R>(rhs), message, loc);
}

template <class L, class R>
inline void ASSERT_LE(L &&lhs, R &&rhs, std::string_view message = {}, const std::source_location &loc = std::source_location::current()) {
    require_le(std::forward<L>(lhs), std::forward<R>(rhs), message, loc);
}

template <class L, class R>
inline void ASSERT_GT(L &&lhs, R &&rhs, std::string_view message = {}, const std::source_location &loc = std::source_location::current()) {
    require_gt(std::forward<L>(lhs), std::forward<R>(rhs), message, loc);
}

template <class L, class R>
inline void ASSERT_GE(L &&lhs, R &&rhs, std::string_view message = {}, const std::source_location &loc = std::source_location::current()) {
    require_ge(std::forward<L>(lhs), std::forward<R>(rhs), message, loc);
}

// Module-friendly exception assertions. Include-based consumers can keep using
// the macro forms below; `import gentest;` consumers use these function
// templates instead.
template <class Expected, class Fn> inline void EXPECT_THROW(Fn &&fn, const std::source_location &loc = std::source_location::current()) {
    ::gentest::detail::expect_throw<Expected>(std::forward<Fn>(fn), detail_internal::expected_exception_name<Expected>(), loc);
}

template <class Fn> inline void EXPECT_NO_THROW(Fn &&fn, const std::source_location &loc = std::source_location::current()) {
    ::gentest::detail::expect_no_throw(std::forward<Fn>(fn), loc);
}

template <class Expected, class Fn> inline void ASSERT_THROW(Fn &&fn, const std::source_location &loc = std::source_location::current()) {
    ::gentest::detail::require_throw<Expected>(std::forward<Fn>(fn), detail_internal::expected_exception_name<Expected>(), loc);
}

template <class Fn> inline void ASSERT_NO_THROW(Fn &&fn, const std::source_location &loc = std::source_location::current()) {
    ::gentest::detail::require_no_throw(std::forward<Fn>(fn), loc);
}

} // namespace asserts

// Unconditionally throw a gentest::failure with the provided message.
inline void fail(std::string_view message) {
#if GENTEST_EXCEPTIONS_ENABLED
    throw failure(std::string(message));
#else
    ::gentest::detail::record_failure(std::string(message));
    ::gentest::detail::terminate_no_exceptions_fatal("gentest::fail");
#endif
}

} // namespace gentest

// gtest-like exception macros (implemented on top of gentest's source_location-based reporting).
// These are optional and can be disabled by defining GENTEST_NO_THROW_MACROS.
#ifndef GENTEST_NO_THROW_MACROS

#define EXPECT_THROW(statement, exception_type)                                                                                            \
    do {                                                                                                                                   \
        ::gentest::detail::expect_throw<exception_type>([&] { statement; }, #exception_type, std::source_location::current());             \
    } while (false)

#define EXPECT_NO_THROW(statement)                                                                                                         \
    do {                                                                                                                                   \
        ::gentest::detail::expect_no_throw([&] { statement; }, std::source_location::current());                                           \
    } while (false)

#define ASSERT_THROW(statement, exception_type)                                                                                            \
    do {                                                                                                                                   \
        ::gentest::detail::require_throw<exception_type>([&] { statement; }, #exception_type, std::source_location::current());            \
    } while (false)

#define ASSERT_NO_THROW(statement)                                                                                                         \
    do {                                                                                                                                   \
        ::gentest::detail::require_no_throw([&] { statement; }, std::source_location::current());                                          \
    } while (false)

#endif // GENTEST_NO_THROW_MACROS
