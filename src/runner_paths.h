#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace gentest::runner {

inline auto argumentPath(std::string_view argument, bool utf8) -> std::filesystem::path {
    if (utf8)
        return std::u8string(argument.begin(), argument.end());
    return {argument};
}

inline auto reportPathText(const std::filesystem::path &path) -> std::string {
    const auto bytes = path.generic_u8string();
    return {bytes.begin(), bytes.end()};
}

} // namespace gentest::runner
