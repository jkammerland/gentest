#include "pcm_cache.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace {
namespace fs = std::filesystem;

bool write_file(const fs::path &path, std::string_view content) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    return static_cast<bool>(output);
}

std::string read_file(const fs::path &path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

bool require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "PCM cache test failed: " << message << '\n';
    }
    return condition;
}

gentest::codegen::PcmCacheContext context_for(const fs::path &source, const fs::path &dependency) {
    return gentest::codegen::PcmCacheContext{
        .module_name            = "gentest.test.module",
        .source                 = source.string(),
        .normalized_command     = "clang++ -std=c++20 --precompile module.cppm",
        .working_directory      = source.parent_path().string(),
        .include_roots          = {source.parent_path().string()},
        .file_dependencies      = {source.string(), dependency.string()},
        .transitive_module_keys = {},
        .compiler_identity      = "unit-compiler",
        .compiler_version       = "unit-version",
        .resource_dir           = source.parent_path().string(),
        .sysroot                = {},
        .scan_deps_identity     = "unit-scan-deps",
        .scan_deps_artifact     = "unit-scan-artifact",
        .options                = "unit-options",
        .salt                   = "unit-salt",
    };
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "usage: gentest_pcm_cache_tests <work-dir>\n";
        return 2;
    }

    const fs::path  root = fs::path{argv[1]} / "pcm_cache_unit";
    std::error_code ec;
    fs::remove_all(root, ec);
    ec.clear();
    fs::create_directories(root, ec);
    if (!require(!ec, "create work directory")) {
        return 1;
    }

    const fs::path source      = root / "module.cppm";
    const fs::path dependency  = root / "dependency.hpp";
    const fs::path source_pcm  = root / "fresh.pcm";
    const fs::path destination = root / "materialized.pcm";
    if (!require(write_file(source, "export module gentest.test.module;\n"), "write source") ||
        !require(write_file(dependency, "inline constexpr int value = 1;\n"), "write dependency") ||
        !require(write_file(source_pcm, "validated-pcm-bytes"), "write PCM")) {
        return 1;
    }

    const auto                         context = context_for(source, dependency);
    gentest::codegen::PcmArtifactCache cache(root / "cache");
    const auto                         key = cache.prepare(context);
    if (!require(key.has_value(), "prepare cacheable closure")) {
        return 1;
    }
    cache.store(context, source_pcm, *key);
    const auto prepared_again = cache.prepare(context);
    if (!require(prepared_again == key, "prepare existing cache entry") ||
        !require(cache.load_prepared(*prepared_again, destination), "load prepared entry before mutation") ||
        !require(read_file(destination) == "validated-pcm-bytes", "materialize exact cached PCM bytes")) {
        return 1;
    }
    fs::remove(destination, ec);
    if (!require(!ec, "remove first materialized PCM") ||
        !require(write_file(dependency, "inline constexpr int value = 2;\n"), "mutate dependency after prepare") ||
        !require(!cache.load_prepared(*prepared_again, destination), "re-fingerprint closure immediately before materialization") ||
        !require(!fs::exists(destination), "leave destination absent after stale prepared lookup")) {
        return 1;
    }

    if (!require(write_file(dependency, "inline constexpr int value = 1;\n"), "restore dependency") ||
        !require(cache.prepare(context) == key, "prepare closure before source mutation") ||
        !require(write_file(source, "export module gentest.test.changed;\n"), "mutate source after prepare") ||
        !require(!cache.load_prepared(*key, destination), "re-fingerprint source immediately before materialization") ||
        !require(!fs::exists(destination), "leave destination absent after stale source lookup")) {
        return 1;
    }

    fs::remove_all(root, ec);
    return 0;
}
