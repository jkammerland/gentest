// Implementation of platform/tooling helpers

#include "tooling_support.hpp"

#include <algorithm>
#include <cctype>
#include <clang/Tooling/ArgumentsAdjusters.h>
#include <filesystem>
#include <iostream>
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

#if defined(__APPLE__)
    // macOS: Detect SDK and system include paths

    // 1. Check for Homebrew LLVM installation (takes precedence)
    std::vector<fs::path> homebrew_llvm_candidates = {
        "/opt/homebrew/opt/llvm@20/include/c++/v1",
        "/opt/homebrew/opt/llvm/include/c++/v1",
        "/usr/local/opt/llvm@20/include/c++/v1",
        "/usr/local/opt/llvm/include/c++/v1"
    };

    for (const auto &candidate : homebrew_llvm_candidates) {
        if (fs::exists(candidate) && fs::is_directory(candidate)) {
            append_unique(candidate);
            // Also add the clang resource directory
            // candidate is /opt/homebrew/opt/llvm@20/include/c++/v1
            // We need to go up to /opt/homebrew/opt/llvm@20, then down to lib/clang
            auto llvm_base = candidate.parent_path().parent_path().parent_path(); // go up from include/c++/v1 to base
            auto clang_include = llvm_base / "lib/clang";
            if (fs::exists(clang_include) && fs::is_directory(clang_include)) {
                // Find the version directory (e.g., "20")
                for (const auto &version_entry : fs::directory_iterator(clang_include)) {
                    if (version_entry.is_directory()) {
                        auto version_include = version_entry.path() / "include";
                        if (fs::exists(version_include)) {
                            append_unique(version_include);
                            break;
                        }
                    }
                }
            }
            break; // Found Homebrew LLVM, use it
        }
    }

    // 2. Try to find SDK path
    std::vector<fs::path> sdk_candidates = {
        "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk",
        "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk"
    };

    fs::path sdk_path;
    for (const auto &candidate : sdk_candidates) {
        if (fs::exists(candidate) && fs::is_directory(candidate)) {
            sdk_path = candidate;
            break;
        }
    }

    if (sdk_path.empty()) {
        // Try to find any MacOSX*.sdk in Command Line Tools
        fs::path sdks_dir = "/Library/Developer/CommandLineTools/SDKs";
        if (fs::exists(sdks_dir) && fs::is_directory(sdks_dir)) {
            for (const auto &entry : fs::directory_iterator(sdks_dir)) {
                if (entry.is_directory() && entry.path().filename().string().find("MacOSX") == 0) {
                    sdk_path = entry.path();
                    break;
                }
            }
        }
    }

    if (!sdk_path.empty()) {
        // Add system headers from SDK
        append_unique(sdk_path / "usr/include");
    } else {
        // Fallback: warn that SDK was not found
        std::cerr << "gentest_codegen: warning: macOS SDK not found, system headers may not be available\n";
    }

#elif defined(__linux__)
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
