// Persistent cache for textual gentest codegen parse results.
#pragma once

#include "model.hpp"

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace gentest::codegen {

struct ParseInputSnapshot {
    std::string path;
    std::string hash;
    std::string unique_id;
};

struct ParseLookupSnapshot {
    std::string path;
    bool        exists = false;
};

struct TextualParseResult {
    int                          status             = 0;
    bool                         had_test_errors    = false;
    bool                         had_fixture_errors = false;
    bool                         had_mock_errors    = false;
    std::vector<TestCaseInfo>    cases;
    std::vector<FixtureDeclInfo> fixtures;
    std::vector<MockClassInfo>   mocks;
    std::vector<std::string>     dependencies;
    // Diagnostics are captured per TU in TU-wrapper mode so a cached warning
    // is reported exactly as it was on a cold parse.
    std::string diagnostics;
    // Paths which were absent (or otherwise did not win lookup) while Clang
    // resolved an #include or __has_include.  Rechecking them prevents a new
    // earlier include-root header from turning a stale result into a hit.
    std::vector<std::string> shadow_guards;
    // PCH and command-line forced-input paths do not necessarily pass through
    // normal textual include callbacks, so keep them in the input fingerprint.
    std::vector<std::string> command_input_guards;
    // Exact buffers and file identities observed by Clang during the cold
    // parse. These are intentionally not serialized; store() uses them to
    // prove inputs did not change between parsing and cache publication.
    std::vector<ParseInputSnapshot> parse_input_snapshots;
    // HeaderSearch's existence decision for every positive/negative lookup
    // guard. These are likewise publication-only and are not serialized.
    std::vector<ParseLookupSnapshot> parse_lookup_snapshots;
    bool                             cacheable = true;
};

struct ParseCacheContext {
    std::string              source;
    std::string              adjusted_command;
    std::string              working_directory;
    std::vector<std::string> include_roots;
    std::string              tool_identity;
    std::string              salt;
    std::string              parse_policy;
};

// Cache reads and writes are deliberately best-effort.  Any malformed,
// unavailable, stale, or concurrently-replaced entry is reported as a miss.
class TextualParseCache {
  public:
    struct FileFingerprint {
        std::string path;
        // weakly_canonical physical identity invalidates byte-identical
        // symlink retargets, which can still alter Clang FileEntry semantics.
        std::string identity;
        // Device/inode identity also detects a byte-identical replacement at
        // the same canonical spelling.
        std::string unique_id;
        std::string hash;
        bool        exists  = false;
        bool        regular = false;
    };

    explicit TextualParseCache(std::filesystem::path directory);

    [[nodiscard]] bool enabled() const { return !directory_.empty(); }
    [[nodiscard]] bool load(const ParseCacheContext &context, TextualParseResult &result);
    void               store(const ParseCacheContext &context, const TextualParseResult &result);

    // Gives callers a content identity for the running codegen executable.
    // An empty value disables caching rather than weakening invalidation.
    [[nodiscard]] static std::string executable_identity(const std::filesystem::path &path);

  private:
    [[nodiscard]] std::optional<FileFingerprint> fingerprint_file(const std::string &path, bool allow_memoization = true);
    [[nodiscard]] std::string                    slot_for(const ParseCacheContext &context) const;
    [[nodiscard]] bool                           fingerprint_matches(const FileFingerprint &expected);

    std::filesystem::path                            directory_;
    std::mutex                                       mutex_;
    std::unordered_map<std::string, FileFingerprint> file_fingerprints_;
};

} // namespace gentest::codegen
