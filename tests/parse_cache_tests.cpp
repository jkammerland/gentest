#include "parse_cache.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

namespace {
namespace fs = std::filesystem;

bool write_file(const fs::path &path, std::string_view content) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    return static_cast<bool>(output);
}

gentest::codegen::ParseCacheContext context_for(const fs::path &source, std::string salt) {
    return gentest::codegen::ParseCacheContext{
        .source            = source.string(),
        .adjusted_command  = "clang++ -std=c++20 -c cases.cpp",
        .working_directory = source.parent_path().string(),
        .include_roots     = {},
        .tool_identity     = "parse-cache-unit-tool",
        .salt              = std::move(salt),
        .parse_policy      = "unit",
    };
}

bool require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "parse cache test failed: " << message << '\n';
    }
    return condition;
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "usage: gentest_parse_cache_tests <work-dir>\n";
        return 2;
    }

    const fs::path  root = fs::path{argv[1]} / "parse_cache_unit";
    std::error_code ec;
    fs::remove_all(root, ec);
    ec.clear();
    fs::create_directories(root, ec);
    if (!require(!ec, "create work directory")) {
        return 1;
    }

    const fs::path source     = root / "cases.cpp";
    const fs::path dependency = root / "dependency.hpp";
    if (!require(write_file(source, "void cached_case() {}\n"), "write source") ||
        !require(write_file(dependency, "inline constexpr int value = 1;\n"), "write dependency")) {
        return 1;
    }

    gentest::codegen::TextualParseResult dependency_result;
    dependency_result.dependencies.push_back(dependency.string());
    const auto                          dependency_a = context_for(source, "dependency-a");
    const auto                          dependency_b = context_for(source, "dependency-b");
    gentest::codegen::TextualParseCache dependency_writer(root / "dependency-cache");
    dependency_writer.store(dependency_a, dependency_result);
    dependency_writer.store(dependency_b, dependency_result);

    gentest::codegen::TextualParseCache  dependency_reader(root / "dependency-cache");
    gentest::codegen::TextualParseResult loaded;
    if (!require(dependency_reader.load(dependency_a, loaded), "load first dependency entry") ||
        !require(write_file(dependency, "inline constexpr int value = 2;\n"), "mutate dependency") ||
        !require(!dependency_reader.load(dependency_b, loaded), "re-fingerprint a dependency changed between loads")) {
        return 1;
    }

    const fs::path                       shadow = root / "previously_missing.hpp";
    gentest::codegen::TextualParseResult shadow_result;
    shadow_result.shadow_guards.push_back(shadow.string());
    const auto                          shadow_a = context_for(source, "shadow-a");
    const auto                          shadow_b = context_for(source, "shadow-b");
    gentest::codegen::TextualParseCache shadow_writer(root / "shadow-cache");
    shadow_writer.store(shadow_a, shadow_result);
    shadow_writer.store(shadow_b, shadow_result);

    gentest::codegen::TextualParseCache shadow_reader(root / "shadow-cache");
    if (!require(shadow_reader.load(shadow_a, loaded), "load first missing-shadow entry") ||
        !require(write_file(shadow, "#pragma once\n"), "create previously missing shadow") ||
        !require(!shadow_reader.load(shadow_b, loaded), "re-check an absent shadow between loads")) {
        return 1;
    }

    const fs::path oversized_input = root / "oversized-input.hpp";
    if (!require(write_file(oversized_input, {}), "create oversized input placeholder")) {
        return 1;
    }
    fs::resize_file(oversized_input, std::uintmax_t{256} * 1024U * 1024U + 1U, ec);
    if (!require(!ec, "create sparse oversized input")) {
        return 1;
    }
    gentest::codegen::TextualParseResult oversized_result;
    oversized_result.dependencies.push_back(oversized_input.string());
    const auto                          oversized_context = context_for(source, "oversized-input");
    gentest::codegen::TextualParseCache oversized_cache(root / "oversized-cache");
    oversized_cache.store(oversized_context, oversized_result);
    if (!require(!oversized_cache.load(oversized_context, loaded), "oversized dependency is a safe cache miss")) {
        return 1;
    }

    const fs::path oversized_executable = root / "oversized-executable";
    if (!require(write_file(oversized_executable, {}), "create oversized executable placeholder")) {
        return 1;
    }
    ec.clear();
    fs::resize_file(oversized_executable, std::uintmax_t{1024} * 1024U * 1024U + 1U, ec);
    if (!require(!ec, "create sparse oversized executable") ||
        !require(gentest::codegen::TextualParseCache::executable_identity(oversized_executable).empty(),
                 "oversized executable identity is rejected")) {
        return 1;
    }

    fs::remove_all(root, ec);
    return 0;
}
