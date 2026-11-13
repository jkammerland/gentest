#pragma once

#include "gentest/runner.h"

#include <concepts>
#include <fmt/core.h>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace gentest {

namespace detail {

// fmt has renamed the type returned by fmt::runtime() across major versions.
using RuntimeFormatString = decltype(fmt::runtime(std::string_view{}));

// A format string that also captures the caller's source location.
//
// A defaulted `std::source_location` parameter cannot follow a function
// parameter pack: the pack is then a non-deduced context. The fmt overloads
// below therefore take the location through their format-string parameter.
// Wrapping this type in `std::type_identity_t` keeps `A...` deduced from the
// trailing arguments only, so the string literal converts here at the call
// site and the defaulted location names the caller.
template <class... A> struct FormatWithLocation {
    fmt::format_string<A...> format;
    std::source_location     location;

    // Checked at compile time whenever fmt checks its own format strings.
    template <class S>
        requires(!std::same_as<S, RuntimeFormatString> && std::constructible_from<fmt::format_string<A...>, const S &>)
    // NOLINTNEXTLINE(google-explicit-constructor): implicit so that a string literal converts at the call site.
    FMT_CONSTEVAL FormatWithLocation(const S &text, const std::source_location &loc = std::source_location::current())
        : format(text), location(loc) {}

    // NOLINTNEXTLINE(google-explicit-constructor): mirrors fmt's implicit runtime format-string conversion.
    FormatWithLocation(RuntimeFormatString text, const std::source_location &loc = std::source_location::current())
        : format(text), location(loc) {}
};

// Formatting arguments for the overloads below. A lone `std::source_location`
// is the location argument of the plain message overloads, not a format
// argument.
template <class... A>
inline constexpr bool format_message_arguments =
    sizeof...(A) > 0 && !(sizeof...(A) == 1 && (std::is_same_v<std::remove_cvref_t<A>, std::source_location> && ...));

template <class... A> inline std::string format_message(const FormatWithLocation<A...> &fmt_str, A &&...a) {
    return fmt::format(fmt_str.format, std::forward<A>(a)...);
}

} // namespace detail

// Convenience: log with fmt (evaluated immediately)
template <class... A> inline void logf(fmt::format_string<A...> fmt_str, A &&...a) { log(fmt::format(fmt_str, std::forward<A>(a)...)); }

// fmt-enabled assertion overloads (messages only formatted and recorded on failure).
// Failures are reported at the caller's location.
template <class... A>
    requires detail::format_message_arguments<A...>
inline void expect(bool condition, std::type_identity_t<detail::FormatWithLocation<A...>> fmt_str, A &&...a) {
    ::gentest::detail::require_owner_context("assertion/expectation called");
    if (!condition)
        expect(condition, std::string_view(detail::format_message(fmt_str, std::forward<A>(a)...)), fmt_str.location);
}

template <class... A>
    requires detail::format_message_arguments<A...>
inline void expect_true(bool condition, std::type_identity_t<detail::FormatWithLocation<A...>> fmt_str, A &&...a) {
    ::gentest::detail::require_owner_context("assertion/expectation called");
    if (!condition)
        expect_true(condition, std::string_view(detail::format_message(fmt_str, std::forward<A>(a)...)), fmt_str.location);
}

template <class L, class R, class... A>
    requires detail::format_message_arguments<A...>
inline void expect_eq(L &&lhs, R &&rhs, std::type_identity_t<detail::FormatWithLocation<A...>> fmt_str, A &&...a) {
    ::gentest::detail::require_owner_context("assertion/expectation called");
    if (!(lhs == rhs))
        expect_eq(std::forward<L>(lhs), std::forward<R>(rhs), std::string_view(detail::format_message(fmt_str, std::forward<A>(a)...)),
                  fmt_str.location);
}

template <class L, class R, class... A>
    requires detail::format_message_arguments<A...>
inline void expect_ne(L &&lhs, R &&rhs, std::type_identity_t<detail::FormatWithLocation<A...>> fmt_str, A &&...a) {
    ::gentest::detail::require_owner_context("assertion/expectation called");
    if (!(lhs != rhs))
        expect_ne(std::forward<L>(lhs), std::forward<R>(rhs), std::string_view(detail::format_message(fmt_str, std::forward<A>(a)...)),
                  fmt_str.location);
}

template <class... A>
    requires detail::format_message_arguments<A...>
inline void require(bool condition, std::type_identity_t<detail::FormatWithLocation<A...>> fmt_str, A &&...a) {
    ::gentest::detail::require_owner_context("assertion/expectation called");
    if (!condition)
        require(condition, std::string_view(detail::format_message(fmt_str, std::forward<A>(a)...)), fmt_str.location);
}

template <class L, class R, class... A>
    requires detail::format_message_arguments<A...>
inline void require_eq(L &&lhs, R &&rhs, std::type_identity_t<detail::FormatWithLocation<A...>> fmt_str, A &&...a) {
    ::gentest::detail::require_owner_context("assertion/expectation called");
    if (!(lhs == rhs))
        require_eq(std::forward<L>(lhs), std::forward<R>(rhs), std::string_view(detail::format_message(fmt_str, std::forward<A>(a)...)),
                   fmt_str.location);
}

template <class L, class R, class... A>
    requires detail::format_message_arguments<A...>
inline void require_ne(L &&lhs, R &&rhs, std::type_identity_t<detail::FormatWithLocation<A...>> fmt_str, A &&...a) {
    ::gentest::detail::require_owner_context("assertion/expectation called");
    if (!(lhs != rhs))
        require_ne(std::forward<L>(lhs), std::forward<R>(rhs), std::string_view(detail::format_message(fmt_str, std::forward<A>(a)...)),
                   fmt_str.location);
}

namespace asserts {
template <class... A>
    requires ::gentest::detail::format_message_arguments<A...>
inline void EXPECT_TRUE(bool condition, std::type_identity_t<::gentest::detail::FormatWithLocation<A...>> fmt_str, A &&...a) {
    ::gentest::expect_true<A...>(condition, fmt_str, std::forward<A>(a)...);
}
template <class L, class R, class... A>
    requires ::gentest::detail::format_message_arguments<A...>
inline void EXPECT_EQ(L &&lhs, R &&rhs, std::type_identity_t<::gentest::detail::FormatWithLocation<A...>> fmt_str, A &&...a) {
    ::gentest::expect_eq<L, R, A...>(std::forward<L>(lhs), std::forward<R>(rhs), fmt_str, std::forward<A>(a)...);
}
template <class L, class R, class... A>
    requires ::gentest::detail::format_message_arguments<A...>
inline void EXPECT_NE(L &&lhs, R &&rhs, std::type_identity_t<::gentest::detail::FormatWithLocation<A...>> fmt_str, A &&...a) {
    ::gentest::expect_ne<L, R, A...>(std::forward<L>(lhs), std::forward<R>(rhs), fmt_str, std::forward<A>(a)...);
}

template <class... A>
    requires ::gentest::detail::format_message_arguments<A...>
inline void ASSERT_TRUE(bool condition, std::type_identity_t<::gentest::detail::FormatWithLocation<A...>> fmt_str, A &&...a) {
    ::gentest::require<A...>(condition, fmt_str, std::forward<A>(a)...);
}
template <class L, class R, class... A>
    requires ::gentest::detail::format_message_arguments<A...>
inline void ASSERT_EQ(L &&lhs, R &&rhs, std::type_identity_t<::gentest::detail::FormatWithLocation<A...>> fmt_str, A &&...a) {
    ::gentest::require_eq<L, R, A...>(std::forward<L>(lhs), std::forward<R>(rhs), fmt_str, std::forward<A>(a)...);
}
template <class L, class R, class... A>
    requires ::gentest::detail::format_message_arguments<A...>
inline void ASSERT_NE(L &&lhs, R &&rhs, std::type_identity_t<::gentest::detail::FormatWithLocation<A...>> fmt_str, A &&...a) {
    ::gentest::require_ne<L, R, A...>(std::forward<L>(lhs), std::forward<R>(rhs), fmt_str, std::forward<A>(a)...);
}
} // namespace asserts

} // namespace gentest
