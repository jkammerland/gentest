#include "pcm_cache.hpp"

#include <barrier>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
namespace fs = std::filesystem;

bool write_file(const fs::path &path, std::string_view content) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    return static_cast<bool>(output);
}

bool make_tree_removable(const fs::path &root) {
    std::error_code ec;
    const auto      root_status = fs::symlink_status(root, ec);
    if (ec == std::errc::no_such_file_or_directory || !fs::exists(root_status)) {
        return true;
    }
    if (ec || fs::is_symlink(root_status)) {
        return false;
    }
    for (fs::recursive_directory_iterator iterator(root, ec), end; !ec && iterator != end; iterator.increment(ec)) {
        const auto status = iterator->symlink_status(ec);
        if (ec) {
            return false;
        }
        if (!fs::is_symlink(status)) {
            fs::permissions(iterator->path(), fs::perms::owner_write, fs::perm_options::add, ec);
            if (ec) {
                return false;
            }
        }
    }
    fs::permissions(root, fs::perms::owner_write, fs::perm_options::add, ec);
    return !ec;
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

bool require_semantic_bypass(std::vector<std::string> command, std::string_view expected_reason, std::string_view label) {
    const auto reason = gentest::codegen::pcm_cache_unsupported_semantic_input(command);
    return require(reason.has_value() && *reason == expected_reason, label);
}

bool require_no_semantic_bypass(std::vector<std::string> command, std::string_view label) {
    return require(!gentest::codegen::pcm_cache_unsupported_semantic_input(command).has_value(), label);
}

gentest::codegen::PcmCacheContext context_for(const fs::path &source, const fs::path &dependency, const fs::path &external_pcm) {
    return gentest::codegen::PcmCacheContext{
        .module_name               = "gentest.test.module",
        .source                    = source.string(),
        .normalized_command        = "clang++ -std=c++20 --precompile module.cppm",
        .working_directory         = source.parent_path().string(),
        .include_roots             = {source.parent_path().string()},
        .file_dependencies         = {source.string(), dependency.string()},
        .external_pcm_dependencies = {external_pcm.string()},
        .transitive_module_keys    = {},
        .compiler_identity         = "unit-compiler",
        .compiler_version          = "unit-version",
        .resource_dir              = source.parent_path().string(),
        .sysroot                   = {},
        .scan_deps_identity        = "unit-scan-deps",
        .scan_deps_artifact        = "unit-scan-artifact",
        .options                   = "unit-options",
        .salt                      = "unit-salt",
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
    if (!require(make_tree_removable(root), "make prior work directory removable")) {
        return 1;
    }
    fs::remove_all(root, ec);
    if (!require(!ec && !fs::exists(root), "remove prior work directory")) {
        return 1;
    }
    ec.clear();
    fs::create_directories(root, ec);
    if (!require(!ec, "create work directory")) {
        return 1;
    }

    if (!require_no_semantic_bypass({"clang++", "-std=c++20"}, "accept a command without untracked semantic inputs") ||
        !require_semantic_bypass({"clang++", "-ivfsoverlay", "overlay.json"}, "a VFS overlay or source remap is active",
                                 "bypass VFS overlays") ||
        !require_semantic_bypass({"clang++", "-remap-file=from;to"}, "a VFS overlay or source remap is active", "bypass source remaps") ||
        !require_semantic_bypass({"clang++", "-chain-include", "prior.pch"}, "a precompiled-header input is active",
                                 "bypass chained PCH inputs") ||
        !require_semantic_bypass({"clang-cl", "/clang:/Yuprior.hpp"}, "a precompiled-header input is active",
                                 "bypass clang-cl PCH inputs") ||
        !require_semantic_bypass({"clang++", "-fmodules", "-fimplicit-modules"}, "a Clang header-module input is active",
                                 "bypass implicit Clang header modules") ||
        !require_semantic_bypass({"clang-cl", "/clang:-fmodule-map-file=module.modulemap"}, "a Clang header-module input is active",
                                 "bypass explicit Clang module maps") ||
        !require_semantic_bypass({"clang++", "-Xclang", "-ast-merge", "-Xclang", "merged.ast"}, "a serialized AST merge input is active",
                                 "bypass split serialized AST merge inputs") ||
        !require_semantic_bypass({"clang-cl", "/clang:-ast-merge=merged.ast"}, "a serialized AST merge input is active",
                                 "bypass joined serialized AST merge inputs") ||
        !require_semantic_bypass({"clang++", "-Xclang", "-load", "plugin.so"}, "a compiler plugin is active", "bypass compiler plugins")) {
        return 1;
    }

    const fs::path source       = root / "module.cppm";
    const fs::path dependency   = root / "dependency.hpp";
    const fs::path external_pcm = root / "external.pcm";
    const fs::path empty_header = root / "empty.hpp";
    const fs::path source_pcm   = root / "fresh.pcm";
    const fs::path destination  = root / "materialized.pcm";
    if (!require(write_file(source, "export module gentest.test.module;\n"), "write source") ||
        !require(write_file(dependency, "inline constexpr int value = 1;\n"), "write dependency") ||
        !require(write_file(empty_header, ""), "write empty dependency") ||
        !require(write_file(external_pcm, "external-pcm-v1"), "write external PCM") ||
        !require(write_file(source_pcm, "validated-pcm-bytes"), "write PCM")) {
        return 1;
    }

    const auto                         context = context_for(source, dependency, external_pcm);
    gentest::codegen::PcmArtifactCache cache(root / "cache");
    const auto                         key = cache.prepare(context);
    if (!key.has_value()) {
        require(false, "prepare cacheable closure");
        return 1;
    }
    const std::string &cache_key = *key;

    auto empty_dependency_context = context;
    empty_dependency_context.file_dependencies.push_back(empty_header.string());
    empty_dependency_context.salt = "unit-empty-dependency";
    gentest::codegen::PcmArtifactCache empty_dependency_cache(root / "empty-dependency-cache");
    const auto                         empty_dependency_key = empty_dependency_cache.prepare(empty_dependency_context);
    if (!empty_dependency_key.has_value()) {
        require(false, "accept an empty regular header dependency");
        return 1;
    }
    const std::string &empty_dependency_cache_key = *empty_dependency_key;
    empty_dependency_cache.store(empty_dependency_context, source_pcm, empty_dependency_cache_key);
    if (!require(empty_dependency_cache.load_prepared(empty_dependency_cache_key, destination),
                 "load a PCM keyed by an empty header dependency") ||
        !require(read_file(destination) == "validated-pcm-bytes", "materialize PCM with empty header dependency")) {
        return 1;
    }
    fs::remove(destination, ec);
    if (!require(!ec, "remove PCM materialized for empty dependency") ||
        !require(write_file(empty_header, "#pragma once\n"), "make empty dependency nonempty") ||
        !require(!empty_dependency_cache.load_prepared(empty_dependency_cache_key, destination),
                 "invalidate an empty dependency when content appears") ||
        !require(!fs::exists(destination), "leave destination absent after empty dependency mutation") ||
        !require(write_file(empty_header, ""), "restore empty dependency")) {
        return 1;
    }
    cache.store(context, source_pcm, cache_key);
    const auto prepared_again = cache.prepare(context);
    if (!prepared_again.has_value()) {
        require(false, "prepare existing cache entry");
        return 1;
    }
    const std::string &prepared_cache_key = *prepared_again;
    if (!require(prepared_cache_key == cache_key, "prepare existing cache entry") ||
        !require(cache.load_prepared(prepared_cache_key, destination), "load prepared entry before mutation") ||
        !require(read_file(destination) == "validated-pcm-bytes", "materialize exact cached PCM bytes")) {
        return 1;
    }
    fs::remove(destination, ec);
    const auto dependency_write_time = fs::last_write_time(dependency, ec);
    if (!require(!ec, "read dependency write time")) {
        return 1;
    }
    fs::last_write_time(dependency, dependency_write_time + std::chrono::seconds{10}, ec);
    if (!require(!ec, "advance dependency write time without changing bytes")) {
        return 1;
    }
    const auto mtime_key = cache.prepare(context);
    if (!require(mtime_key.has_value() && mtime_key != key, "write-time-only change produces a distinct key") ||
        !require(!cache.load_prepared(prepared_cache_key, destination), "write-time-only change rejects the prepared entry") ||
        !require(!fs::exists(destination), "leave destination absent after write-time-only change")) {
        return 1;
    }
    fs::last_write_time(dependency, dependency_write_time, ec);
    if (!require(!ec, "restore dependency write time") ||
        !require(cache.prepare(context) == key, "restored write time restores the original key") ||
        !require(write_file(dependency, "inline constexpr int value = 2;\n"), "mutate dependency after prepare") ||
        !require(!cache.load_prepared(prepared_cache_key, destination), "re-fingerprint closure immediately before materialization") ||
        !require(!fs::exists(destination), "leave destination absent after stale prepared lookup")) {
        return 1;
    }

    if (!require(write_file(dependency, "inline constexpr int value = 1;\n"), "restore dependency")) {
        return 1;
    }
    const auto source_race_key = cache.prepare(context);
    if (!source_race_key.has_value()) {
        require(false, "prepare closure before source mutation");
        return 1;
    }
    const std::string &source_race_cache_key = *source_race_key;
    cache.store(context, source_pcm, source_race_cache_key);
    if (!require(cache.load_prepared(source_race_cache_key, destination), "load current entry before source mutation") ||
        !require(read_file(destination) == "validated-pcm-bytes", "materialize current PCM before source mutation")) {
        return 1;
    }
    fs::remove(destination, ec);
    if (!require(!ec, "remove PCM materialized before source mutation") ||
        !require(write_file(source, "export module gentest.test.changed;\n"), "mutate source after prepare") ||
        !require(!cache.load_prepared(source_race_cache_key, destination), "re-fingerprint source immediately before materialization") ||
        !require(!fs::exists(destination), "leave destination absent after stale source lookup")) {
        return 1;
    }

    if (!require(write_file(source, "export module gentest.test.module;\n"), "restore source")) {
        return 1;
    }
    auto external_store_context = context;
    external_store_context.salt = "unit-external-store-race";
    gentest::codegen::PcmArtifactCache external_store_cache(root / "external-store-race-cache");
    const auto                         external_store_key = external_store_cache.prepare(external_store_context);
    if (!external_store_key.has_value()) {
        require(false, "prepare closure before external PCM store race");
        return 1;
    }
    const std::string &external_store_cache_key = *external_store_key;
    if (!require(write_file(external_pcm, "external-pcm-v2"), "mutate external PCM before store")) {
        return 1;
    }
    external_store_cache.store(external_store_context, source_pcm, external_store_cache_key);
    if (!require(!fs::exists(root / "external-store-race-cache" / external_store_cache_key),
                 "do not publish an entry after external PCM changes before store") ||
        !require(write_file(external_pcm, "external-pcm-v1"), "restore external PCM before load race")) {
        return 1;
    }
    const auto external_load_key = cache.prepare(context);
    if (!external_load_key.has_value()) {
        require(false, "prepare closure before external PCM mutation");
        return 1;
    }
    const std::string &external_load_cache_key = *external_load_key;
    cache.store(context, source_pcm, external_load_cache_key);
    if (!require(cache.load_prepared(external_load_cache_key, destination), "load current entry before external PCM mutation") ||
        !require(read_file(destination) == "validated-pcm-bytes", "materialize current PCM before external PCM mutation")) {
        return 1;
    }
    fs::remove(destination, ec);
    if (!require(!ec, "remove PCM materialized before external PCM mutation") ||
        !require(write_file(external_pcm, "external-pcm-v2"), "mutate external PCM after prepare") ||
        !require(!cache.load_prepared(external_load_cache_key, destination),
                 "re-fingerprint external PCM immediately before materialization") ||
        !require(!fs::exists(destination), "leave destination absent after stale external PCM lookup")) {
        return 1;
    }

    if (!require(write_file(external_pcm, "external-pcm-v1"), "restore external PCM before concurrent publication")) {
        return 1;
    }
    auto race_context                                    = context;
    race_context.salt                                    = "unit-racing-writers";
    constexpr std::size_t                   writer_count = 4;
    std::barrier                            start_gate(static_cast<std::ptrdiff_t>(writer_count));
    std::vector<std::optional<std::string>> writer_keys(writer_count);
    std::vector<std::thread>                writers;
    writers.reserve(writer_count);
    for (std::size_t index = 0; index < writer_count; ++index) {
        writers.emplace_back([&, index] {
            gentest::codegen::PcmArtifactCache writer(root / "concurrent-cache");
            writer_keys[index] = writer.prepare(race_context);
            start_gate.arrive_and_wait();
            if (writer_keys[index].has_value()) {
                writer.store(race_context, source_pcm, *writer_keys[index]);
            }
        });
    }
    for (auto &writer : writers) {
        writer.join();
    }
    if (!require(writer_keys.front().has_value(), "prepare concurrent writer key")) {
        return 1;
    }
    for (const auto &writer_key : writer_keys) {
        if (!require(writer_key == writer_keys.front(), "concurrent writers agree on one cache key")) {
            return 1;
        }
    }
    gentest::codegen::PcmArtifactCache concurrent_reader(root / "concurrent-cache");
    const auto                         concurrent_key = concurrent_reader.prepare(race_context);
    if (!concurrent_key.has_value()) {
        require(false, "prepare concurrently published entry");
        return 1;
    }
    const std::string &concurrent_cache_key = *concurrent_key;
    if (!require(concurrent_key == writer_keys.front(), "prepare concurrently published entry") ||
        !require(concurrent_reader.load_prepared(concurrent_cache_key, destination), "load concurrently published entry") ||
        !require(read_file(destination) == "validated-pcm-bytes", "concurrent publication preserved exact PCM bytes")) {
        return 1;
    }
    for (const auto &entry : fs::directory_iterator(root / "concurrent-cache")) {
        const std::string name = entry.path().filename().string();
        if (!require(!(name.starts_with('.') && name.find(".tmp.") != std::string::npos),
                     "concurrent writers clean temporary cache directories")) {
            return 1;
        }
    }

    if (!require(make_tree_removable(root), "make cache entries removable after test")) {
        return 1;
    }
    ec.clear();
    fs::remove_all(root, ec);
    if (!require(!ec && !fs::exists(root), "remove work directory after test")) {
        return 1;
    }
    return 0;
}
