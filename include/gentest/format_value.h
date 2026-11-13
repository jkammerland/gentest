#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <fmt/format.h>
#include <iomanip>
#include <limits>
#include <optional>
#include <ostream>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <variant>

namespace gentest::detail {

template <typename T>
concept ValueStreamInsertable = requires(std::ostream &stream, const T &value) { stream << value; };

template <typename T>
concept ValueFormattable = ValueStreamInsertable<T> || fmt::is_formattable<T, char>::value;

// Pointers to signed/unsigned bytes (`unsigned char *`, `std::uint8_t *`,
// `signed char *`, any cv-qualification). The standard stream inserters read
// them as NUL-terminated strings, but they usually address binary buffers.
template <typename T>
inline constexpr bool byte_pointer = std::is_pointer_v<T> && (std::is_same_v<std::remove_cv_t<std::remove_pointer_t<T>>, unsigned char> ||
                                                              std::is_same_v<std::remove_cv_t<std::remove_pointer_t<T>>, signed char>);

template <typename T>
    requires ValueFormattable<T>
inline std::string format_printable_value(const T &value, bool use_boolalpha) {
    if constexpr (byte_pointer<T>) {
        // Print the address, like any other object pointer; never read through it.
        if (value == nullptr)
            return "nullptr";
        std::ostringstream stream;
        stream << static_cast<const void *>(const_cast<const std::remove_cv_t<std::remove_pointer_t<T>> *>(value));
        return stream.str();
    } else if constexpr (ValueStreamInsertable<T>) {
        std::ostringstream stream;
        if (use_boolalpha) {
            stream << std::boolalpha;
        }
        if constexpr (std::is_floating_point_v<T>) {
            stream << std::setprecision(std::numeric_limits<T>::max_digits10);
        }
        // Ordinary function pointers use the standard boolean representation.
        // Avoid MSVC's function-to-object pointer extension, while preserving
        // actual manipulators and custom free insertion operators.
        if constexpr (std::is_function_v<std::remove_pointer_t<std::remove_cvref_t<T>>> &&
                      !std::is_convertible_v<T, std::ostream &(*)(std::ostream &)> &&
                      !std::is_convertible_v<T, std::ios &(*)(std::ios &)> &&
                      !std::is_convertible_v<T, std::ios_base &(*)(std::ios_base &)> && !requires { operator<<(stream, value); }) {
            stream << static_cast<bool>(value);
        } else {
            stream << value;
        }
        return stream.str();
    } else {
        return fmt::format("{}", value);
    }
}

} // namespace gentest::detail

namespace gentest::detail {

inline std::string quote_diagnostic_string(std::string_view value, char quote = '"') {
    std::string           out(1, quote);
    constexpr std::size_t limit = 512;
    auto                  count = std::min(value.size(), limit);
    // Keep a truncated UTF-8 code point out of otherwise valid string output.
    if (count < value.size()) {
        while (count > 0 && (static_cast<unsigned char>(value[count]) & 0xc0U) == 0x80U)
            --count;
    }
    for (const auto ch : value.substr(0, count)) {
        switch (ch) {
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\0': out += "\\0"; break;
        default:
            if (ch == quote) {
                out += '\\';
                out += ch;
            } else if (static_cast<unsigned char>(ch) < 32 || ch == '\x7f') {
                out += fmt::format("\\x{:02x}", static_cast<unsigned char>(ch));
            } else {
                out += ch;
            }
        }
    }
    out += quote;
    if (value.size() > limit)
        out += fmt::format("... ({} bytes)", value.size());
    return out;
}

template <typename T>
inline constexpr bool diagnostic_string =
    !std::is_null_pointer_v<std::remove_cvref_t<T>> && std::is_convertible_v<const T &, std::string_view>;

// Character arrays are bounded by their extent and end at the first NUL, so
// padded buffers such as `char buf[8] = "ab"` read as "ab". Character pointers
// follow the C-string convention: they must point to a NUL-terminated string.
template <typename T> auto diagnostic_string_view(const T &value) -> std::string_view {
    if constexpr (std::is_array_v<T>) {
        constexpr auto size = std::extent_v<T>;
        const auto    *end  = std::char_traits<char>::find(value, size, '\0');
        return {value, end != nullptr ? static_cast<std::size_t>(end - value) : size};
    } else {
        return std::string_view(value);
    }
}

template <typename T> struct IsDiagnosticOptional : std::false_type {};
template <typename T> struct IsDiagnosticOptional<std::optional<T>> : std::true_type {};
template <typename T> struct IsDiagnosticVariant : std::false_type {};
template <typename... T> struct IsDiagnosticVariant<std::variant<T...>> : std::true_type {};
template <typename T> struct IsDiagnosticTuple : std::false_type {};
template <typename... T> struct IsDiagnosticTuple<std::tuple<T...>> : std::true_type {};
template <typename A, typename B> struct IsDiagnosticTuple<std::pair<A, B>> : std::true_type {};

template <typename T> std::string format_diagnostic_value(const T &value, std::size_t depth = 0) {
    using Value = std::remove_cvref_t<T>;
    if (depth >= 8)
        return "... (depth limit)";

    if constexpr (std::is_null_pointer_v<Value>) {
        return "nullptr";
    } else if constexpr (diagnostic_string<T>) {
        if constexpr (std::is_pointer_v<T>) {
            if (value == nullptr)
                return "nullptr";
        }
        return quote_diagnostic_string(diagnostic_string_view(value));
    } else if constexpr (std::is_same_v<Value, char>) {
        return quote_diagnostic_string(std::string_view(&value, 1), '\'');
    } else if constexpr (std::is_same_v<Value, signed char> || std::is_same_v<Value, unsigned char>) {
        return fmt::format("{}", static_cast<int>(value));
    } else if constexpr (std::is_same_v<Value, std::byte>) {
        return fmt::format("{}", std::to_integer<unsigned int>(value));
    } else if constexpr (ValueFormattable<T> && !std::is_array_v<T>) {
        // Respect user stream/fmt customizations before structural fallbacks.
        return format_printable_value(value, true);
    } else if constexpr (IsDiagnosticOptional<Value>::value) {
        return value ? "optional(" + format_diagnostic_value(*value, depth + 1) + ")" : "nullopt";
    } else if constexpr (IsDiagnosticVariant<Value>::value) {
        if (value.valueless_by_exception())
            return "variant(valueless)";
        return std::visit(
            [&](const auto &item) { return fmt::format("variant[{}]({})", value.index(), format_diagnostic_value(item, depth + 1)); },
            value);
    } else if constexpr (std::is_same_v<Value, std::monostate>) {
        return "monostate";
    } else if constexpr (std::ranges::forward_range<const T>) {
        std::string out   = "[";
        std::size_t index = 0;
        for (const auto &item : value) {
            if (index != 0)
                out += ", ";
            if (index++ == 32) {
                out += "...";
                break;
            }
            out += format_diagnostic_value(item, depth + 1);
        }
        return out + "]";
    } else if constexpr (IsDiagnosticTuple<Value>::value) {
        std::string out   = "(";
        std::size_t index = 0;
        std::apply([&](const auto &...items) { ((out += (index++ == 0 ? "" : ", ") + format_diagnostic_value(items, depth + 1)), ...); },
                   value);
        return out + ")";
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

// Compare forward ranges only: diagnostics must not consume a single-pass source.
template <typename L, typename R> std::string comparison_mismatch(const L &lhs, const R &rhs) {
    if constexpr (diagnostic_string<L> && diagnostic_string<R>) {
        if constexpr (std::is_pointer_v<L>) {
            if (!lhs)
                return {};
        }
        if constexpr (std::is_pointer_v<R>) {
            if (!rhs)
                return {};
        }
        const auto  left  = diagnostic_string_view(lhs);
        const auto  right = diagnostic_string_view(rhs);
        std::size_t index = 0, line = 1, column = 1;
        while (index < left.size() && index < right.size() && left[index] == right[index]) {
            if (left[index] == '\n') {
                ++line;
                column = 1;
            } else {
                ++column;
            }
            ++index;
        }
        if (index == left.size() && index == right.size()) {
            if constexpr (std::is_pointer_v<L> && std::is_pointer_v<R>) {
                // Built-in == on character pointers compares addresses, not contents.
                if (static_cast<const void *>(lhs) != static_cast<const void *>(rhs))
                    return "; operands are distinct pointers to equal strings (character pointers compare by address; compare "
                           "std::string_view values to compare contents)";
            }
            return {};
        }
        return fmt::format("; first difference at line {}, column {} (byte {}): {} versus {}", line, column, index,
                           index < left.size() ? format_diagnostic_value(left[index]) : "<end>",
                           index < right.size() ? format_diagnostic_value(right[index]) : "<end>");
    } else if constexpr (std::ranges::forward_range<const L> && std::ranges::forward_range<const R>) {
        auto       left      = std::ranges::begin(lhs);
        auto       right     = std::ranges::begin(rhs);
        const auto left_end  = std::ranges::end(lhs);
        const auto right_end = std::ranges::end(rhs);
        if constexpr (requires {
                          { *left == *right } -> std::convertible_to<bool>;
                      }) {
            std::size_t index = 0;
            while (left != left_end && right != right_end && index < 4096) {
                if (!(*left == *right))
                    return fmt::format("; first difference at index {}: {} versus {}", index, format_diagnostic_value(*left),
                                       format_diagnostic_value(*right));
                ++left;
                ++right;
                ++index;
            }
            if ((left == left_end) != (right == right_end))
                return fmt::format("; first difference at index {}: {} versus {}", index,
                                   left == left_end ? "<end>" : format_diagnostic_value(*left),
                                   right == right_end ? "<end>" : format_diagnostic_value(*right));
        }
        return {};
    } else {
        return {};
    }
}

} // namespace gentest::detail

namespace gentest {

// Bounded recursive diagnostics, preserving custom stream/fmt representations.
template <typename T> inline std::string format_value(const T &value) { return detail::format_diagnostic_value(value); }

} // namespace gentest
