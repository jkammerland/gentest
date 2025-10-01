// Shared model types for gentest codegen
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gentest::codegen {

struct ParsedAttribute {
    std::string              name;
    std::vector<std::string> arguments;
};

struct AttributeCollection {
    std::vector<ParsedAttribute> gentest;
    std::vector<std::string>     other_namespaces;
};

struct CollectorOptions {
    std::string                          entry = "gentest::run_all_tests";
    std::filesystem::path                output_path;
    std::filesystem::path                template_path;
    std::vector<std::string>             sources;
    std::vector<std::string>             clang_args;
    std::optional<std::filesystem::path> compilation_database;
    bool                                 check_only = false;
};

struct TestCaseInfo {
    std::string              qualified_name;
    std::string              display_name;
    std::string              filename;
    unsigned                 line = 0;
    std::vector<std::string> tags;
    std::vector<std::string> requirements;
    bool                     should_skip = false;
    std::string              skip_reason;
};

} // namespace gentest::codegen

