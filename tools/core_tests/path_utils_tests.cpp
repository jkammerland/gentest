#include "path_utils.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

static std::string ascii_upper_copy(std::string value) {
    for (char &ch : value) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return value;
}

int main() {
#if !defined(_WIN32)
    // This is a Windows-specific regression test (case-insensitive roots + drive letters).
    return 0;
#else
    int failures = 0;
    auto expect  = [&](bool ok, const char *msg) {
        if (!ok) {
            ++failures;
            std::cerr << "FAIL: " << msg << "\n";
        }
    };

    const fs::path root = fs::current_path() / "path_utils_case_test" / "SubDir";
    const fs::path leaf = root / "file.hpp";

    std::error_code ec;
    fs::remove_all(root.parent_path(), ec);
    ec.clear();
    fs::create_directories(root, ec);
    expect(!ec, "create_directories succeeds");

    {
        std::ofstream out(leaf);
        out << "\n";
    }

    const std::string root_str = root.generic_string();
    const std::string leaf_str = leaf.generic_string();

    // Force case differences: typical source of Windows include-root issues.
    const fs::path root_upper = fs::path(ascii_upper_copy(root_str));
    const fs::path leaf_lower = fs::path(gentest::codegen::ascii_lower_copy(leaf_str));

    expect(gentest::codegen::is_path_within(leaf_lower, root_upper), "is_path_within treats roots case-insensitively on Windows");

    fs::remove_all(root.parent_path(), ec);

    if (failures) {
        std::cerr << "Total failures: " << failures << "\n";
        return 1;
    }
    return 0;
#endif
}

