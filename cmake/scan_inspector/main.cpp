#include "source_inspection.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

void print_usage() {
    std::cerr << "usage: gentest_scan_inspector --inspect-source <source> [--inspect-include-dir <dir> ...] [-- <clang-args...>]\n";
}

} // namespace

int main(int argc, char **argv) {
    std::vector<std::string>           positional;
    std::vector<std::filesystem::path> include_dirs;
    std::vector<std::string>           command_line_args;
    bool                               command_line_mode = false;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (!command_line_mode && arg == "--") {
            command_line_mode = true;
            continue;
        }
        if (command_line_mode) {
            command_line_args.emplace_back(arg);
            continue;
        }
        if (arg == "--inspect-source") {
            continue;
        }
        if (arg == "--inspect-include-dir") {
            if (i + 1 >= argc) {
                print_usage();
                return 1;
            }
            include_dirs.emplace_back(argv[++i]);
            continue;
        }
        positional.emplace_back(arg);
    }

    if (positional.size() != 1) {
        print_usage();
        return 1;
    }

    const std::filesystem::path source_path = positional.front();
    if (!std::filesystem::exists(source_path)) {
        std::cerr << "gentest_scan_inspector: source '" << source_path.string() << "' does not exist\n";
        return 1;
    }

    const auto inspection = gentest::codegen::inspect_source(source_path, include_dirs, command_line_args);
    std::cout << "module_name=";
    if (inspection.module_name.has_value()) {
        std::cout << *inspection.module_name;
    }
    std::cout << "\nimports_gentest_mock=" << (inspection.imports_gentest_mock ? "1" : "0") << '\n';
    return 0;
}
