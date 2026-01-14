// Common filesystem/path helpers for gentest codegen.
#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

namespace gentest::codegen {

namespace fs = std::filesystem;

inline fs::path normalize_path(const fs::path &path) {
    std::error_code ec;
    fs::path        out = path;
    if (!out.is_absolute()) {
        out = fs::absolute(out, ec);
        if (ec) {
            return path;
        }
    }
    ec.clear();
    out = fs::weakly_canonical(out, ec);
    if (ec) {
        return path;
    }
    return out;
}

inline std::string ascii_lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

inline bool is_path_within(const fs::path &path, const fs::path &root) {
    if (root.empty()) {
        return false;
    }

    const std::string path_str_raw = normalize_path(path).generic_string();
    std::string       root_str_raw = normalize_path(root).generic_string();

#if defined(_WIN32)
    const std::string path_str = ascii_lower_copy(path_str_raw);
    std::string       root_str = ascii_lower_copy(std::move(root_str_raw));
#else
    const std::string &path_str = path_str_raw;
    std::string        root_str = std::move(root_str_raw);
#endif

    if (path_str == root_str) {
        return true;
    }
    if (!root_str.empty() && root_str.back() != '/') {
        root_str.push_back('/');
    }
    return path_str.rfind(root_str, 0) == 0;
}

} // namespace gentest::codegen

