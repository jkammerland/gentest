#pragma once

#include <fmt/format.h>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <typeinfo>

namespace gentest::detail {

template <typename T>
concept ValueStreamInsertable = requires(std::ostream &stream, const T &value) { stream << value; };

template <typename T>
concept ValueFormattable = ValueStreamInsertable<T> || fmt::is_formattable<T, char>::value;

template <typename T>
    requires ValueFormattable<T>
inline std::string format_printable_value(const T &value, bool use_boolalpha) {
    if constexpr (ValueStreamInsertable<T>) {
        std::ostringstream stream;
        if (use_boolalpha) {
            stream << std::boolalpha;
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

namespace gentest {

// Format a value using Gentest's diagnostic-compatible stream/fmt selection.
// Values without either representation use the established unprintable fallback.
template <typename T> inline std::string format_value(const T &value) {
    if constexpr (detail::ValueFormattable<T>) {
        return detail::format_printable_value(value, true);
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

} // namespace gentest
