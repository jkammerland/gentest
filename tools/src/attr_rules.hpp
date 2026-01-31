// Internal attribute rules shared by generator (and future tooling)
#pragma once

#include <algorithm>
#include <array>
#include <string_view>

namespace gentest::detail {

// Value attributes that fall through to the generic validation branch.
inline constexpr std::array<std::string_view, 1> kAllowedValueAttributes{"owner"};
inline constexpr std::array<std::string_view, 5> kAllowedFlagAttributes{"fast", "slow", "linux", "windows", "death"};
inline constexpr std::array<std::string_view, 1> kAllowedFixtureAttributes{"fixture"};

inline bool is_allowed_value_attribute(std::string_view name) {
    return std::ranges::find(kAllowedValueAttributes, name) != kAllowedValueAttributes.end();
}

inline bool is_allowed_flag_attribute(std::string_view name) {
    return std::ranges::find(kAllowedFlagAttributes, name) != kAllowedFlagAttributes.end();
}

inline bool is_allowed_fixture_attribute(std::string_view name) {
    return std::ranges::find(kAllowedFixtureAttributes, name) != kAllowedFixtureAttributes.end();
}

} // namespace gentest::detail
