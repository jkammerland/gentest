#pragma once

#include <type_traits>

namespace gentest {

enum class LogPolicy : unsigned {
    Never     = 0,
    OnFailure = 1u << 0,
    Always    = OnFailure | (1u << 1),
};

constexpr auto to_underlying(LogPolicy policy) noexcept -> std::underlying_type_t<LogPolicy> {
    return static_cast<std::underlying_type_t<LogPolicy>>(policy);
}

constexpr auto operator|(LogPolicy lhs, LogPolicy rhs) noexcept -> LogPolicy {
    return static_cast<LogPolicy>(to_underlying(lhs) | to_underlying(rhs));
}

constexpr auto operator|=(LogPolicy &lhs, LogPolicy rhs) noexcept -> LogPolicy & {
    lhs = lhs | rhs;
    return lhs;
}

} // namespace gentest
