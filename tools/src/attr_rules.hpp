// Internal attribute rules shared by generator (and future tooling)
#pragma once

#include <algorithm>
#include <array>
#include <string_view>

namespace gentest::detail {

inline constexpr std::array<std::string_view, 2> kAllowedValueAttributes{"category", "owner"};
inline constexpr std::array<std::string_view, 4> kAllowedFlagAttributes{"fast", "slow", "linux", "windows"};
inline constexpr std::array<std::string_view, 1> kAllowedFixtureFlags{"stateful_fixture"};

inline bool is_allowed_value_attribute(std::string_view name) {
    return std::find(kAllowedValueAttributes.begin(), kAllowedValueAttributes.end(), name) != kAllowedValueAttributes.end();
}

inline bool is_allowed_flag_attribute(std::string_view name) {
    return std::find(kAllowedFlagAttributes.begin(), kAllowedFlagAttributes.end(), name) != kAllowedFlagAttributes.end();
}

inline bool is_allowed_fixture_flag(std::string_view name) {
    return std::find(kAllowedFixtureFlags.begin(), kAllowedFixtureFlags.end(), name) != kAllowedFixtureFlags.end();
}

} // namespace gentest::detail
