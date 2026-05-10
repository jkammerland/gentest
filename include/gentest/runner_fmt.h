#pragma once

#include "gentest/runner.h"

#include <fmt/core.h>

namespace gentest {

// Convenience: log with fmt (evaluated immediately)
template <class... A> inline void logf(fmt::format_string<A...> fmt_str, A &&...a) { log(fmt::format(fmt_str, std::forward<A>(a)...)); }

// fmt-enabled assertion overloads (messages only formatted and recorded on failure)
template <class... A>
    requires(sizeof...(A) > 0)
inline void expect(bool condition, fmt::format_string<A...> fmt_str, A &&...a,
                   const std::source_location &loc = std::source_location::current()) {
    ::gentest::detail::require_owner_context("assertion/expectation called");
    if (!condition)
        expect(condition, std::string_view(fmt::format(fmt_str, std::forward<A>(a)...)), loc);
}

template <class... A>
    requires(sizeof...(A) > 0)
inline void expect_true(bool condition, fmt::format_string<A...> fmt_str, A &&...a,
                        const std::source_location &loc = std::source_location::current()) {
    ::gentest::detail::require_owner_context("assertion/expectation called");
    if (!condition)
        expect_true(condition, std::string_view(fmt::format(fmt_str, std::forward<A>(a)...)), loc);
}

template <class L, class R, class... A>
    requires(sizeof...(A) > 0)
inline void expect_eq(L &&lhs, R &&rhs, fmt::format_string<A...> fmt_str, A &&...a,
                      const std::source_location &loc = std::source_location::current()) {
    ::gentest::detail::require_owner_context("assertion/expectation called");
    if (!(lhs == rhs))
        expect_eq(std::forward<L>(lhs), std::forward<R>(rhs), std::string_view(fmt::format(fmt_str, std::forward<A>(a)...)), loc);
}

template <class L, class R, class... A>
    requires(sizeof...(A) > 0)
inline void expect_ne(L &&lhs, R &&rhs, fmt::format_string<A...> fmt_str, A &&...a,
                      const std::source_location &loc = std::source_location::current()) {
    ::gentest::detail::require_owner_context("assertion/expectation called");
    if (!(lhs != rhs))
        expect_ne(std::forward<L>(lhs), std::forward<R>(rhs), std::string_view(fmt::format(fmt_str, std::forward<A>(a)...)), loc);
}

template <class... A>
    requires(sizeof...(A) > 0)
inline void require(bool condition, fmt::format_string<A...> fmt_str, A &&...a,
                    const std::source_location &loc = std::source_location::current()) {
    ::gentest::detail::require_owner_context("assertion/expectation called");
    if (!condition)
        require(condition, std::string_view(fmt::format(fmt_str, std::forward<A>(a)...)), loc);
}

template <class L, class R, class... A>
    requires(sizeof...(A) > 0)
inline void require_eq(L &&lhs, R &&rhs, fmt::format_string<A...> fmt_str, A &&...a,
                       const std::source_location &loc = std::source_location::current()) {
    ::gentest::detail::require_owner_context("assertion/expectation called");
    if (!(lhs == rhs))
        require_eq(std::forward<L>(lhs), std::forward<R>(rhs), std::string_view(fmt::format(fmt_str, std::forward<A>(a)...)), loc);
}

template <class L, class R, class... A>
    requires(sizeof...(A) > 0)
inline void require_ne(L &&lhs, R &&rhs, fmt::format_string<A...> fmt_str, A &&...a,
                       const std::source_location &loc = std::source_location::current()) {
    ::gentest::detail::require_owner_context("assertion/expectation called");
    if (!(lhs != rhs))
        require_ne(std::forward<L>(lhs), std::forward<R>(rhs), std::string_view(fmt::format(fmt_str, std::forward<A>(a)...)), loc);
}

namespace asserts {
template <class... A>
    requires(sizeof...(A) > 0)
inline void EXPECT_TRUE(bool condition, fmt::format_string<A...> fmt_str, A &&...a) {
    ::gentest::expect_true<A...>(condition, fmt_str, std::forward<A>(a)...);
}
template <class L, class R, class... A>
    requires(sizeof...(A) > 0)
inline void EXPECT_EQ(L &&lhs, R &&rhs, fmt::format_string<A...> fmt_str, A &&...a) {
    ::gentest::expect_eq<L, R, A...>(std::forward<L>(lhs), std::forward<R>(rhs), fmt_str, std::forward<A>(a)...);
}
template <class L, class R, class... A>
    requires(sizeof...(A) > 0)
inline void EXPECT_NE(L &&lhs, R &&rhs, fmt::format_string<A...> fmt_str, A &&...a) {
    ::gentest::expect_ne<L, R, A...>(std::forward<L>(lhs), std::forward<R>(rhs), fmt_str, std::forward<A>(a)...);
}

template <class... A>
    requires(sizeof...(A) > 0)
inline void ASSERT_TRUE(bool condition, fmt::format_string<A...> fmt_str, A &&...a) {
    ::gentest::require<A...>(condition, fmt_str, std::forward<A>(a)...);
}
template <class L, class R, class... A>
    requires(sizeof...(A) > 0)
inline void ASSERT_EQ(L &&lhs, R &&rhs, fmt::format_string<A...> fmt_str, A &&...a) {
    ::gentest::require_eq<L, R, A...>(std::forward<L>(lhs), std::forward<R>(rhs), fmt_str, std::forward<A>(a)...);
}
template <class L, class R, class... A>
    requires(sizeof...(A) > 0)
inline void ASSERT_NE(L &&lhs, R &&rhs, fmt::format_string<A...> fmt_str, A &&...a) {
    ::gentest::require_ne<L, R, A...>(std::forward<L>(lhs), std::forward<R>(rhs), fmt_str, std::forward<A>(a)...);
}
} // namespace asserts

} // namespace gentest
