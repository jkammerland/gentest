#include "scan_utils.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

bool contains_path(const std::vector<std::filesystem::path> &paths, const std::filesystem::path &needle) {
    return std::find(paths.begin(), paths.end(), needle.lexically_normal()) != paths.end();
}

} // namespace

int main() {
    namespace fs = std::filesystem;

    std::error_code ec;
    const auto      root =
        fs::temp_directory_path() / ("gentest_scan_include_clang_dir_append_" + std::to_string(static_cast<long long>(::getpid())));
    fs::remove_all(root, ec);

    const auto source_dir = root / "src";
    const auto clang_dir  = root / "clang";
    fs::create_directories(source_dir, ec);
    fs::create_directories(clang_dir, ec);
    if (ec) {
        std::cerr << "failed to create fixture directories: " << ec.message() << '\n';
        return 1;
    }

    std::vector<fs::path> expected_include_dirs;
    for (std::size_t idx = 0; idx < 128; ++idx) {
        const auto version_dir = clang_dir / ("v" + std::to_string(idx));
        const auto include_dir = version_dir / "include";
        fs::create_directories(include_dir, ec);
        if (ec) {
            std::cerr << "failed to create clang include dir: " << ec.message() << '\n';
            return 1;
        }
        expected_include_dirs.push_back(include_dir.lexically_normal());
    }

    const auto paths = gentest::codegen::scan::default_scan_include_search_paths(source_dir, {clang_dir});
    for (const auto &expected : expected_include_dirs) {
        if (!contains_path(paths, expected)) {
            std::cerr << "missing expected include path: " << expected << '\n';
            return 1;
        }
    }

    fs::remove_all(root, ec);
    return 0;
}
