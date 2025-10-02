// Implementation of platform/tooling helpers

#include "tooling_support.hpp"

#include <algorithm>
#include <cctype>
#include <clang/Tooling/ArgumentsAdjusters.h>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace gentest::codegen {

using clang::tooling::CommandLineArguments;

static std::vector<int> parse_version_components(std::string_view text) {
    std::vector<int> components;
    std::size_t      pos = 0;
    while (pos < text.size()) {
        if (!std::isdigit(static_cast<unsigned char>(text[pos]))) {
            return {};
        }
        std::size_t end = pos;
        while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end])) != 0) {
            ++end;
        }
        components.push_back(std::stoi(std::string(text.substr(pos, end - pos))));
        if (end >= text.size()) {
            break;
        }
        if (text[end] != '.') {
            return {};
        }
        pos = end + 1;
    }
    return components;
}

static bool version_less(const std::vector<int> &lhs, const std::vector<int> &rhs) {
    return std::ranges::lexicographical_compare(lhs, rhs);
}

auto detect_platform_include_dirs() -> std::vector<std::string> {
    std::vector<std::string> dirs;
#if defined(__linux__)
    namespace fs = std::filesystem;

    auto append_unique = [&dirs](const fs::path &candidate) {
        if (!fs::exists(candidate) || !fs::is_directory(candidate)) {
            return;
        }
        auto normalized = candidate.lexically_normal().string();
        if (std::ranges::find(dirs, normalized) == dirs.end()) {
            dirs.push_back(std::move(normalized));
        }
    };

    auto detect_latest_version = [](const fs::path &root) -> std::optional<fs::path> {
        if (!fs::exists(root) || !fs::is_directory(root)) {
            return std::nullopt;
        }
        std::optional<fs::path> best;
        std::vector<int>        best_version;
        for (const auto &entry : fs::directory_iterator(root)) {
            if (!entry.is_directory()) {
                continue;
            }
            auto version = parse_version_components(entry.path().filename().string());
            if (version.empty()) {
                continue;
            }
            if (!best.has_value() || version_less(best_version, version)) {
                best         = entry.path();
                best_version = std::move(version);
            }
        }
        return best;
    };

    if (auto cxx_root = detect_latest_version("/usr/include/c++")) {
        append_unique(*cxx_root);

        fs::path architecture_dir;
        for (const auto &entry : fs::directory_iterator(*cxx_root)) {
            if (!entry.is_directory()) {
                continue;
            }
            auto name = entry.path().filename().string();
            if (name.find("-linux") != std::string::npos || name.find("-gnu") != std::string::npos) {
                architecture_dir = entry.path();
                break;
            }
        }
        if (!architecture_dir.empty()) {
            append_unique(architecture_dir);
        }

        append_unique(*cxx_root / "backward");
    }

    auto detect_gcc_internal = [&](const fs::path &root) -> std::optional<fs::path> {
        if (!fs::exists(root) || !fs::is_directory(root)) {
            return std::nullopt;
        }

        std::optional<fs::path> best;
        std::vector<int>        best_version;

        for (const auto &triple_dir : fs::directory_iterator(root)) {
            if (!triple_dir.is_directory()) {
                continue;
            }
            for (const auto &version_dir : fs::directory_iterator(triple_dir.path())) {
                if (!version_dir.is_directory()) {
                    continue;
                }
                auto version = parse_version_components(version_dir.path().filename().string());
                if (version.empty()) {
                    continue;
                }
                if (!best.has_value() || version_less(best_version, version)) {
                    best         = version_dir.path();
                    best_version = std::move(version);
                }
            }
        }

        if (best) {
            fs::path include_dir = *best / "include";
            if (fs::exists(include_dir)) {
                return std::optional<fs::path>{include_dir};
            }
        }
        return std::optional<fs::path>{};
    };

    if (auto internal = detect_gcc_internal("/usr/lib/gcc")) {
        append_unique(*internal);
    }
    if (auto internal = detect_gcc_internal("/usr/lib64/gcc")) {
        append_unique(*internal);
    }

    append_unique(fs::path("/usr/include"));
#endif
    return dirs;
}

bool contains_isystem_entry(const CommandLineArguments &args, const std::string &dir) {
    for (std::size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == "-isystem" && args[i + 1] == dir) {
            return true;
        }
    }
    return false;
}

} // namespace gentest::codegen
