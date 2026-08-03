#pragma once

#include <fmt/format.h>
#include <ostream>
#include <sstream>
#include <string>
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
        stream << value;
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
