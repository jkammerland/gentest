// Opt-in persistent cache for validated named-module PCM artifacts.
#pragma once

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace gentest::codegen {

// The context intentionally contains the complete current scan-deps closure.
// A caller must leave file_dependencies empty when it cannot prove that
// closure; PcmArtifactCache will then bypass both reads and writes.
struct PcmCacheContext {
    std::string              module_name;
    std::string              source;
    std::string              normalized_command;
    std::string              working_directory;
    std::vector<std::string> include_roots;
    std::vector<std::string> file_dependencies;
    std::vector<std::string> transitive_module_keys;
    std::string              compiler_identity;
    std::string              compiler_version;
    std::string              resource_dir;
    std::string              sysroot;
    std::string              scan_deps_identity;
    std::string              scan_deps_artifact;
    std::string              options;
    std::string              salt;
};

// This is deliberately separate from TextualParseCache. A cache entry is an
// immutable directory containing both the PCM and a self-checking manifest.
// Any inability to fully validate the current closure is a normal miss.
class PcmArtifactCache {
  public:
    struct FileFingerprint;

    explicit PcmArtifactCache(std::filesystem::path directory);

    [[nodiscard]] bool enabled() const { return !directory_.empty(); }

    // A bounded content identity for an already-materialized external PCM.
    // Callers use this only when the artifact has no validated-cache key of
    // its own (for example an imported public/package module).
    [[nodiscard]] static std::optional<std::string> content_identity(const std::filesystem::path &path);

    // Resolves a tool symlink to its regular-file target and returns a bounded
    // streaming content identity. Cache artifacts intentionally remain strict
    // and use content_identity() instead.
    [[nodiscard]] static std::optional<std::string> executable_identity(const std::filesystem::path &path);

    // Hashes the current complete closure once and retains its context for a
    // matching load. An empty result means the input is not safe
    // to cache (for example, scan-deps did not provide file-deps).
    [[nodiscard]] std::optional<std::string> prepare(const PcmCacheContext &context) const;

    // Materializes a fully validated cached PCM to destination through a
    // unique temporary. `key` receives the current closure key on both a hit
    // and a miss when the context was cacheable.
    [[nodiscard]] bool load_prepared(std::string_view key, const std::filesystem::path &destination) const;

    // Best-effort publication. Only a complete, regular, non-symlink PCM is
    // accepted; existing cache slots are never replaced or removed.
    void store(const PcmCacheContext &context, const std::filesystem::path &pcm, std::string_view expected_key) const;

  private:
    struct InputBundle;

    [[nodiscard]] std::optional<InputBundle> input_bundle(const PcmCacheContext &context, bool allow_fingerprint_memo) const;

    std::filesystem::path                                    directory_;
    mutable std::mutex                                       prepared_mutex_;
    mutable std::unordered_map<std::string, PcmCacheContext> prepared_inputs_;
};

} // namespace gentest::codegen
