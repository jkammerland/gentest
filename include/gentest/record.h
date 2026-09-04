#pragma once

#include "gentest/detail/runtime_base.h"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace gentest {

enum class RecordScope { Current, Case, Suite, Run };

struct RecordOptions {
    RecordScope      scope = RecordScope::Current;
    std::string_view schema;
};

// Own strings and normalize arithmetic types without converting C strings to bool.
struct PropertyValue {
    using Storage = std::variant<std::nullptr_t, bool, std::int64_t, std::uint64_t, double, std::string>;
    Storage value{nullptr};

    PropertyValue() = default;
    PropertyValue(std::nullptr_t) : value(nullptr) {}
    PropertyValue(bool v) : value(v) {}
    template <std::integral T>
        requires(!std::same_as<T, bool> && sizeof(T) <= 8)
    PropertyValue(T v) : value(static_cast<std::conditional_t<std::is_signed_v<T>, std::int64_t, std::uint64_t>>(v)) {}
    template <std::floating_point T>
        requires(sizeof(T) <= sizeof(double))
    PropertyValue(T v) : value(static_cast<double>(v)) {}
    PropertyValue(std::string v) : value(std::move(v)) {}
    PropertyValue(std::string_view v) : value(std::string(v)) {}
    PropertyValue(const char *v) : value(v ? Storage(std::string(v)) : Storage(nullptr)) {}
};

// Owner-context only. Properties replace a key within the selected scope;
// records append owned snapshots. Recording from timed bench/jitter calls fails.
GENTEST_RUNTIME_API void record_property(std::string_view key, PropertyValue value, RecordScope scope = RecordScope::Current,
                                         const std::source_location &loc = std::source_location::current());
GENTEST_RUNTIME_API void record_data(std::string_view name, std::span<const std::byte> bytes, std::string_view content_type,
                                     RecordOptions options = {}, const std::source_location &loc = std::source_location::current());

namespace detail {
// Adapter preflight must run before serialization, particularly in measured cases.
GENTEST_RUNTIME_API bool record_ready(std::string_view name, std::string_view content_type, RecordOptions options,
                                      const std::source_location &loc);
GENTEST_RUNTIME_API void record_error(std::string_view message, const std::source_location &loc);
} // namespace detail

} // namespace gentest
