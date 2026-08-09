#include "pcm_cache.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <llvm/ADT/SmallString.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/JSON.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/Process.h>
#include <llvm/Support/SHA256.h>
#include <llvm/Support/raw_ostream.h>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace gentest::codegen {
namespace {
namespace fs   = std::filesystem;
namespace json = llvm::json;

constexpr std::string_view kSchema                    = "gentest.validated_pcm_cache.v4";
constexpr std::uintmax_t   kMaxPcmBytes               = std::uintmax_t{1024} * 1024U * 1024U;
constexpr std::uintmax_t   kMaxInputBytes             = std::uintmax_t{256} * 1024U * 1024U;
constexpr std::uintmax_t   kMaxExecutableBytes        = std::uintmax_t{1024} * 1024U * 1024U;
constexpr std::uintmax_t   kMaxMetadataBytes          = std::uintmax_t{4} * 1024U * 1024U;
constexpr std::size_t      kMaxFingerprintMemoEntries = 8192;
constexpr std::string_view kPcmFilename               = "module.pcm";
constexpr std::string_view kMetadataFilename          = "entry.json";

std::string sha256_hex(std::string_view content) {
    llvm::SHA256 hasher;
    hasher.update(llvm::StringRef{content.data(), content.size()});
    const auto            digest = hasher.final();
    static constexpr char hex[]  = "0123456789abcdef";
    std::string           out(digest.size() * 2, '\0');
    for (std::size_t idx = 0; idx < digest.size(); ++idx) {
        out[idx * 2]     = hex[(digest[idx] >> 4) & 0x0F];
        out[idx * 2 + 1] = hex[digest[idx] & 0x0F];
    }
    return out;
}

std::optional<std::string> sha256_file(const fs::path &path, std::uintmax_t max_size, std::uintmax_t *size_out = nullptr,
                                       bool allow_empty = false) {
    std::error_code ec;
    const auto      status = fs::symlink_status(path, ec);
    if (ec || fs::is_symlink(status) || !fs::is_regular_file(status)) {
        return std::nullopt;
    }
    auto fd = llvm::sys::fs::openNativeFileForRead(path.string());
    if (!fd) {
        return std::nullopt;
    }
    llvm::sys::fs::file_status file_status;
    if (llvm::sys::fs::status(*fd, file_status) || !llvm::sys::fs::is_regular_file(file_status)) {
        [[maybe_unused]] const std::error_code close_ec = llvm::sys::fs::closeFile(*fd);
        return std::nullopt;
    }
    const std::uintmax_t size = file_status.getSize();
    if ((!allow_empty && size == 0) || size > max_size) {
        [[maybe_unused]] const std::error_code close_ec = llvm::sys::fs::closeFile(*fd);
        return std::nullopt;
    }

    llvm::SHA256                               hasher;
    std::array<char, std::size_t{128} * 1024U> chunk{};
    std::uintmax_t                             remaining = size;
    while (remaining != 0) {
        const std::size_t requested = static_cast<std::size_t>(std::min<std::uintmax_t>(remaining, chunk.size()));
        auto              read      = llvm::sys::fs::readNativeFile(*fd, llvm::MutableArrayRef<char>{chunk.data(), requested});
        if (!read || *read == 0) {
            if (!read) {
                llvm::consumeError(read.takeError());
            }
            [[maybe_unused]] const std::error_code close_ec = llvm::sys::fs::closeFile(*fd);
            return std::nullopt;
        }
        hasher.update(llvm::StringRef{chunk.data(), *read});
        remaining -= *read;
    }
    char extra = 0;
    auto eof   = llvm::sys::fs::readNativeFile(*fd, llvm::MutableArrayRef<char>{&extra, 1});
    if (!eof || *eof != 0) {
        if (!eof) {
            llvm::consumeError(eof.takeError());
        }
        [[maybe_unused]] const std::error_code close_ec = llvm::sys::fs::closeFile(*fd);
        return std::nullopt;
    }
    llvm::sys::fs::file_status final_status;
    const bool                 final_valid =
        !llvm::sys::fs::status(*fd, final_status) && llvm::sys::fs::is_regular_file(final_status) && final_status.getSize() == size;
    const std::error_code close_ec = llvm::sys::fs::closeFile(*fd);
    if (!final_valid || close_ec) {
        return std::nullopt;
    }
    if (size_out != nullptr) {
        *size_out = size;
    }
    const auto            digest = hasher.final();
    static constexpr char hex[]  = "0123456789abcdef";
    std::string           out(digest.size() * 2, '\0');
    for (std::size_t idx = 0; idx < digest.size(); ++idx) {
        out[idx * 2]     = hex[(digest[idx] >> 4) & 0x0F];
        out[idx * 2 + 1] = hex[digest[idx] & 0x0F];
    }
    return out;
}

std::string normalize_path(std::string_view raw, std::string_view base_directory = {}) {
    if (raw.empty()) {
        return {};
    }
    std::error_code ec;
    fs::path        path{std::string{raw}};
    if (path.is_relative()) {
        if (!base_directory.empty()) {
            path = fs::path{std::string{base_directory}} / path;
        }
        if (const fs::path absolute = fs::absolute(path, ec); !ec) {
            path = absolute;
        }
    }
    return path.lexically_normal().generic_string();
}

std::string logical_path(std::string_view raw, std::string_view build_directory) {
    std::string       normalized = normalize_path(raw, build_directory);
    const std::string build      = normalize_path(build_directory);
    if (normalized.empty() || build.empty()) {
        return normalized;
    }
    const fs::path relative    = fs::path{normalized}.lexically_relative(fs::path{build});
    const auto     relative_it = relative.begin();
    if (!relative.empty() && !relative.is_absolute() && relative_it != relative.end() && *relative_it != "..") {
        return (fs::path{"$build"} / relative).generic_string();
    }
    return normalized;
}

void append_length_prefixed(std::string &out, std::string_view value) {
    out += std::to_string(value.size());
    out.push_back(':');
    out.append(value.data(), value.size());
    out.push_back('\0');
}

bool regular_non_symlink_directory(const fs::path &path) {
    std::error_code ec;
    const auto      status = fs::symlink_status(path, ec);
    return !ec && !fs::is_symlink(status) && fs::is_directory(status);
}

std::optional<std::string> read_regular_file(const fs::path &path, std::uintmax_t max_size) {
    std::error_code ec;
    const auto      status = fs::symlink_status(path, ec);
    if (ec || fs::is_symlink(status) || !fs::is_regular_file(status)) {
        return std::nullopt;
    }
    const auto size = fs::file_size(path, ec);
    if (ec || size > max_size) {
        return std::nullopt;
    }
    auto fd = llvm::sys::fs::openNativeFileForRead(path.string());
    if (!fd) {
        return std::nullopt;
    }
    llvm::sys::fs::file_status file_status;
    if (llvm::sys::fs::status(*fd, file_status) || !llvm::sys::fs::is_regular_file(file_status) || file_status.getSize() != size) {
        [[maybe_unused]] const std::error_code close_ec = llvm::sys::fs::closeFile(*fd);
        return std::nullopt;
    }
    auto buffer = llvm::MemoryBuffer::getOpenFile(*fd, path.string(), file_status.getSize(), true, true);
    if (llvm::sys::fs::closeFile(*fd) || !buffer) {
        return std::nullopt;
    }
    return (*buffer)->getBuffer().str();
}

bool make_destination_parent(const fs::path &destination) {
    std::error_code ec;
    fs::create_directories(destination.parent_path(), ec);
    return !ec && regular_non_symlink_directory(destination.parent_path());
}

bool publish_local_file(const fs::path &temporary, const fs::path &destination) {
    std::error_code ec;
    const auto      temporary_status = fs::symlink_status(temporary, ec);
    if (ec || fs::is_symlink(temporary_status) || !fs::is_regular_file(temporary_status)) {
        return false;
    }
#if defined(_WIN32)
    const auto destination_status = fs::symlink_status(destination, ec);
    if (!ec && fs::exists(destination_status)) {
        if (fs::is_symlink(destination_status) || !fs::is_regular_file(destination_status)) {
            return false;
        }
        const bool removed = fs::remove(destination, ec);
        if (ec || !removed) {
            return false;
        }
    } else if (ec && ec != std::errc::no_such_file_or_directory) {
        return false;
    }
    ec.clear();
#endif
    fs::rename(temporary, destination, ec);
    return !ec;
}

struct MaterializationPaths {
    fs::path source;
    fs::path destination;
};

template <typename Validate>
bool materialize_file(const MaterializationPaths &paths, std::string_view expected_hash, std::uintmax_t expected_size,
                      Validate &&validate_before_publish) {
    if (!make_destination_parent(paths.destination)) {
        return false;
    }
    llvm::SmallString<256> temporary_storage;
    int                    temporary_fd = -1;
    const std::string      model =
        (paths.destination.parent_path() / ("." + paths.destination.filename().string() + ".cache.%%%%%%%%")).string();
    if (llvm::sys::fs::createUniqueFile(model, temporary_fd, temporary_storage) || temporary_fd < 0) {
        return false;
    }
    if (llvm::sys::Process::SafelyCloseFileDescriptor(temporary_fd)) {
        std::error_code ignored;
        fs::remove(fs::path{temporary_storage.str().str()}, ignored);
        return false;
    }
    const fs::path temporary{temporary_storage.str().str()};

    std::error_code ec;
    fs::copy_file(paths.source, temporary, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        return false;
    }
    std::uintmax_t copied_size = 0;
    const auto     copied_hash = sha256_file(temporary, kMaxPcmBytes, &copied_size);
    if (!copied_hash.has_value() || copied_size != expected_size || *copied_hash != expected_hash || !validate_before_publish() ||
        !publish_local_file(temporary, paths.destination)) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        return false;
    }
    return true;
}

void cleanup_temporary_directory(const fs::path &directory, const fs::path &temporary) {
    // This is only ever a directory created by createUniqueDirectory directly
    // beneath the verified cache root. Refuse anything else before cleanup so
    // a substituted path cannot widen a best-effort cache cleanup.
    if (temporary.parent_path() != directory || !temporary.filename().string().starts_with('.')) {
        return;
    }
    std::error_code ec;
    const auto      status = fs::symlink_status(temporary, ec);
    if (ec || fs::is_symlink(status) || !fs::is_directory(status)) {
        return;
    }
    fs::remove_all(temporary, ec);
}

} // namespace

struct PcmArtifactCache::FileFingerprint {
    std::string path;
    std::string logical_path;
    std::string identity;
    std::string unique_id;
    std::string write_time;
    std::string hash;
};

struct PcmArtifactCache::InputBundle {
    std::string                  canonical_context;
    FileFingerprint              source;
    std::vector<FileFingerprint> dependencies;
    std::vector<FileFingerprint> external_pcm_dependencies;
    std::string                  key;
};

namespace {

std::optional<PcmArtifactCache::FileFingerprint> fingerprint_file(std::string_view raw_path, std::string_view build_directory,
                                                                  std::uintmax_t max_size, bool allow_memo, bool allow_empty = false) {
    PcmArtifactCache::FileFingerprint fingerprint;
    fingerprint.path         = normalize_path(raw_path, build_directory);
    fingerprint.logical_path = logical_path(fingerprint.path, build_directory);
    if (fingerprint.path.empty() || fingerprint.logical_path.empty()) {
        return std::nullopt;
    }
    std::error_code ec;
    const auto      status = fs::status(fingerprint.path, ec);
    if (ec || !fs::is_regular_file(status)) {
        return std::nullopt;
    }
    const fs::path canonical = fs::weakly_canonical(fingerprint.path, ec);
    if (ec) {
        return std::nullopt;
    }
    fingerprint.identity = canonical.generic_string();
    llvm::sys::fs::file_status llvm_status;
    if (llvm::sys::fs::status(fingerprint.path, llvm_status)) {
        return std::nullopt;
    }
    const auto id         = llvm_status.getUniqueID();
    fingerprint.unique_id = std::to_string(id.getDevice()) + ":" + std::to_string(id.getFile());
    const auto size       = fs::file_size(fingerprint.path, ec);
    if (ec) {
        return std::nullopt;
    }
    const auto write_time = fs::last_write_time(fingerprint.path, ec);
    if (ec) {
        return std::nullopt;
    }
    fingerprint.write_time = std::to_string(write_time.time_since_epoch().count());
    std::string memo_key;
    append_length_prefixed(memo_key, fingerprint.path);
    append_length_prefixed(memo_key, fingerprint.identity);
    append_length_prefixed(memo_key, fingerprint.unique_id);
    append_length_prefixed(memo_key, std::to_string(size));
    append_length_prefixed(memo_key, fingerprint.write_time);
    append_length_prefixed(memo_key, std::to_string(max_size));
    append_length_prefixed(memo_key, allow_empty ? "allow-empty" : "require-content");
    struct FingerprintMemo {
        std::mutex                                                         mutex;
        std::unordered_map<std::string, PcmArtifactCache::FileFingerprint> entries;
    };
    static FingerprintMemo memo;
    if (allow_memo) {
        std::lock_guard lock(memo.mutex);
        if (const auto existing = memo.entries.find(memo_key); existing != memo.entries.end()) {
            return existing->second;
        }
    }
    const auto content_hash = sha256_file(fingerprint.path, max_size, nullptr, allow_empty);
    if (!content_hash.has_value()) {
        return std::nullopt;
    }
    fingerprint.hash = *content_hash;
    if (allow_memo) {
        std::lock_guard lock(memo.mutex);
        if (memo.entries.size() >= kMaxFingerprintMemoEntries) {
            memo.entries.clear();
        }
        memo.entries.insert_or_assign(memo_key, fingerprint);
    }
    return fingerprint;
}

void append_fingerprint_material(std::string &out, std::string_view prefix, const PcmArtifactCache::FileFingerprint &fingerprint) {
    append_length_prefixed(out, prefix);
    append_length_prefixed(out, fingerprint.logical_path);
    append_length_prefixed(out, fingerprint.write_time);
    append_length_prefixed(out, fingerprint.hash);
}

json::Object fingerprint_object(const PcmArtifactCache::FileFingerprint &fingerprint) {
    return json::Object{
        {.K = "path", .V = fingerprint.path},
        {.K = "logical_path", .V = fingerprint.logical_path},
        {.K = "identity", .V = fingerprint.identity},
        {.K = "unique_id", .V = fingerprint.unique_id},
        {.K = "write_time", .V = fingerprint.write_time},
        {.K = "hash", .V = fingerprint.hash},
    };
}

json::Array fingerprint_array(const std::vector<PcmArtifactCache::FileFingerprint> &fingerprints) {
    json::Array out;
    out.reserve(fingerprints.size());
    for (const auto &fingerprint : fingerprints) {
        out.push_back(fingerprint_object(fingerprint));
    }
    return out;
}

bool read_fingerprint(const json::Value &value, PcmArtifactCache::FileFingerprint &out) {
    const auto *object = value.getAsObject();
    if (object == nullptr) {
        return false;
    }
    const auto path       = object->getString("path");
    const auto logical    = object->getString("logical_path");
    const auto identity   = object->getString("identity");
    const auto unique_id  = object->getString("unique_id");
    const auto write_time = object->getString("write_time");
    const auto hash       = object->getString("hash");
    if (!path.has_value() || !logical.has_value() || !identity.has_value() || !unique_id.has_value() || !write_time.has_value() ||
        !hash.has_value()) {
        return false;
    }
    out = PcmArtifactCache::FileFingerprint{.path         = path->str(),
                                            .logical_path = logical->str(),
                                            .identity     = identity->str(),
                                            .unique_id    = unique_id->str(),
                                            .write_time   = write_time->str(),
                                            .hash         = hash->str()};
    return true;
}

bool fingerprints_match(const PcmArtifactCache::FileFingerprint &expected, const PcmArtifactCache::FileFingerprint &actual) {
    if (expected.logical_path != actual.logical_path || expected.write_time != actual.write_time || expected.hash != actual.hash) {
        return false;
    }
    // A byte-identical symlink/hard-link retarget at the same spelling is a
    // miss. A different spelling with the same logical build-relative path is
    // the supported relocated-build-tree case.
    if (expected.path == actual.path) {
        return expected.identity == actual.identity && expected.unique_id == actual.unique_id;
    }
    return true;
}

std::optional<std::string> get_string(const json::Object &object, llvm::StringRef key) {
    if (const auto value = object.getString(key); value.has_value()) {
        return value->str();
    }
    return std::nullopt;
}

} // namespace

std::optional<std::string_view> pcm_cache_unsupported_semantic_input(std::span<const std::string> command_line) {
    for (const auto &raw_arg : command_line) {
        llvm::StringRef arg{raw_arg};
        if (arg.starts_with("/clang:")) {
            arg = arg.drop_front(std::string_view{"/clang:"}.size());
        }
        if (arg == "-ivfsoverlay" || arg == "-vfsoverlay" || arg == "--vfsoverlay" || arg == "-remap-file" ||
            arg.starts_with("-ivfsoverlay=") || arg.starts_with("-vfsoverlay=") || arg.starts_with("--vfsoverlay=") ||
            arg.starts_with("-remap-file=")) {
            return "a VFS overlay or source remap is active";
        }
        if (arg == "-include-pch" || arg == "-include-pth" || arg == "-chain-include" || arg == "-pch-through-header" ||
            arg == "-pch-through-hdrstop-create" || arg == "-pch-through-hdrstop-use" || arg.starts_with("-include-pch=") ||
            arg.starts_with("-include-pth=") || arg.starts_with("-chain-include=") || arg.starts_with("-pch-through-header=") ||
            arg.starts_with("/Yu") || arg.starts_with("/Yc") || arg.starts_with("/Fp")) {
            return "a precompiled-header input is active";
        }
        if (arg == "-fplugin" || arg == "-fpass-plugin" || arg == "-load" || arg == "-load-pass-plugin" || arg == "-plugin" ||
            arg == "-add-plugin" || arg.starts_with("-fplugin=") || arg.starts_with("-fpass-plugin=") ||
            arg.starts_with("-load-pass-plugin=")) {
            return "a compiler plugin is active";
        }
    }
    return std::nullopt;
}

PcmArtifactCache::PcmArtifactCache(fs::path directory) : directory_(std::move(directory)) {}

std::optional<std::string> PcmArtifactCache::executable_identity(const fs::path &path) {
    std::error_code ec;
    const fs::path  resolved = fs::weakly_canonical(path, ec);
    if (ec) {
        return std::nullopt;
    }
    const auto status = fs::symlink_status(resolved, ec);
    if (ec || fs::is_symlink(status) || !fs::is_regular_file(status)) {
        return std::nullopt;
    }
    const auto hash = sha256_file(resolved, kMaxExecutableBytes);
    if (!hash.has_value()) {
        return std::nullopt;
    }
    return resolved.generic_string() + ";sha256=" + *hash;
}

std::optional<PcmArtifactCache::InputBundle> PcmArtifactCache::input_bundle(const PcmCacheContext &context,
                                                                            bool                   allow_fingerprint_memo) const {
    if (!enabled() || context.module_name.empty() || context.source.empty() || context.normalized_command.empty() ||
        context.compiler_identity.empty() || context.compiler_version.empty() || context.resource_dir.empty() ||
        context.scan_deps_identity.empty() || context.scan_deps_artifact.empty() || context.file_dependencies.empty()) {
        return std::nullopt;
    }
    InputBundle bundle;
    bundle.source =
        fingerprint_file(context.source, context.working_directory, kMaxInputBytes, allow_fingerprint_memo).value_or(FileFingerprint{});
    if (bundle.source.path.empty()) {
        return std::nullopt;
    }
    bundle.dependencies.reserve(context.file_dependencies.size());
    for (const auto &dependency : context.file_dependencies) {
        const auto fingerprint = fingerprint_file(dependency, context.working_directory, kMaxInputBytes, allow_fingerprint_memo, true);
        if (!fingerprint.has_value()) {
            return std::nullopt;
        }
        // `file-deps` normally repeats the primary source. Store it once in
        // the explicit source slot, but keep every other current dependency.
        if (fingerprint->logical_path != bundle.source.logical_path || fingerprint->path != bundle.source.path) {
            bundle.dependencies.push_back(*fingerprint);
        }
    }
    std::ranges::sort(bundle.dependencies, {}, &FileFingerprint::logical_path);
    const auto duplicate = std::ranges::adjacent_find(bundle.dependencies, {}, &FileFingerprint::logical_path);
    if (duplicate != bundle.dependencies.end()) {
        // Different physical spellings for one logical input are ambiguous.
        return std::nullopt;
    }
    bundle.external_pcm_dependencies.reserve(context.external_pcm_dependencies.size());
    for (const auto &dependency : context.external_pcm_dependencies) {
        const auto fingerprint = fingerprint_file(dependency, context.working_directory, kMaxPcmBytes, allow_fingerprint_memo);
        if (!fingerprint.has_value()) {
            return std::nullopt;
        }
        bundle.external_pcm_dependencies.push_back(*fingerprint);
    }
    std::ranges::sort(bundle.external_pcm_dependencies, {}, &FileFingerprint::logical_path);
    const auto external_duplicate = std::ranges::adjacent_find(bundle.external_pcm_dependencies, {}, &FileFingerprint::logical_path);
    if (external_duplicate != bundle.external_pcm_dependencies.end()) {
        return std::nullopt;
    }

    std::string context_text;
    append_length_prefixed(context_text, kSchema);
    append_length_prefixed(context_text, context.module_name);
    append_length_prefixed(context_text, context.normalized_command);
    append_length_prefixed(context_text, context.compiler_identity);
    append_length_prefixed(context_text, context.compiler_version);
    append_length_prefixed(context_text, logical_path(context.resource_dir, context.working_directory));
    append_length_prefixed(context_text, logical_path(context.sysroot, context.working_directory));
    append_length_prefixed(context_text, context.scan_deps_identity);
    append_length_prefixed(context_text, context.scan_deps_artifact);
    append_length_prefixed(context_text, context.options);
    append_length_prefixed(context_text, context.salt);
    for (const auto &root : context.include_roots) {
        append_length_prefixed(context_text, logical_path(root, context.working_directory));
    }
    for (const auto &key : context.transitive_module_keys) {
        append_length_prefixed(context_text, key);
    }
    bundle.canonical_context = std::move(context_text);

    std::string material = bundle.canonical_context;
    append_fingerprint_material(material, "source", bundle.source);
    for (const auto &dependency : bundle.dependencies) {
        append_fingerprint_material(material, "dependency", dependency);
    }
    for (const auto &dependency : bundle.external_pcm_dependencies) {
        append_fingerprint_material(material, "external-pcm", dependency);
    }
    bundle.key = sha256_hex(material);
    return bundle;
}

std::optional<std::string> PcmArtifactCache::prepare(const PcmCacheContext &context) const {
    auto bundle = input_bundle(context, true);
    if (!bundle.has_value()) {
        return std::nullopt;
    }
    std::string     key = bundle->key;
    std::lock_guard lock(prepared_mutex_);
    prepared_inputs_.insert_or_assign(key, context);
    return key;
}

bool PcmArtifactCache::load_prepared(std::string_view key, const fs::path &destination) const {
    std::optional<PcmCacheContext> context;
    {
        std::lock_guard lock(prepared_mutex_);
        const auto      prepared = prepared_inputs_.find(std::string(key));
        if (prepared == prepared_inputs_.end()) {
            return false;
        }
        context = prepared->second;
    }
    // The prepared bundle is only a provisional lookup key. Re-fingerprint
    // the complete current closure without the invocation memo immediately
    // before reading or materializing a shared PCM.
    const auto bundle = input_bundle(*context, false);
    if (!bundle.has_value() || bundle->key != key || !regular_non_symlink_directory(directory_)) {
        return false;
    }
    const fs::path entry = directory_ / bundle->key;
    if (!regular_non_symlink_directory(entry)) {
        return false;
    }
    const auto metadata_text = read_regular_file(entry / kMetadataFilename, kMaxMetadataBytes);
    if (!metadata_text.has_value()) {
        return false;
    }
    auto        parsed = json::parse(*metadata_text);
    const auto *root   = parsed ? parsed->getAsObject() : nullptr;
    if (root == nullptr || get_string(*root, "schema") != kSchema || get_string(*root, "context") != bundle->canonical_context ||
        get_string(*root, "cache_key") != bundle->key) {
        return false;
    }
    const auto *source_value              = root->get("source");
    const auto *dependencies              = root->getArray("dependencies");
    const auto *external_pcm_dependencies = root->getArray("external_pcm_dependencies");
    if (source_value == nullptr || dependencies == nullptr || external_pcm_dependencies == nullptr ||
        dependencies->size() != bundle->dependencies.size() ||
        external_pcm_dependencies->size() != bundle->external_pcm_dependencies.size()) {
        return false;
    }
    FileFingerprint stored_source;
    if (!read_fingerprint(*source_value, stored_source) || !fingerprints_match(stored_source, bundle->source)) {
        return false;
    }
    for (std::size_t idx = 0; idx < dependencies->size(); ++idx) {
        FileFingerprint stored_dependency;
        if (!read_fingerprint((*dependencies)[idx], stored_dependency) ||
            !fingerprints_match(stored_dependency, bundle->dependencies[idx])) {
            return false;
        }
    }
    for (std::size_t idx = 0; idx < external_pcm_dependencies->size(); ++idx) {
        FileFingerprint stored_dependency;
        if (!read_fingerprint((*external_pcm_dependencies)[idx], stored_dependency) ||
            !fingerprints_match(stored_dependency, bundle->external_pcm_dependencies[idx])) {
            return false;
        }
    }
    const auto pcm_hash      = get_string(*root, "pcm_sha256");
    const auto pcm_size_text = get_string(*root, "pcm_size");
    if (!pcm_hash.has_value() || !pcm_size_text.has_value()) {
        return false;
    }
    std::uintmax_t pcm_size   = 0;
    const auto     parse_size = std::from_chars(pcm_size_text->data(), pcm_size_text->data() + pcm_size_text->size(), pcm_size);
    if (parse_size.ec != std::errc{} || parse_size.ptr != pcm_size_text->data() + pcm_size_text->size() || pcm_size == 0 ||
        pcm_size > kMaxPcmBytes) {
        return false;
    }
    std::uintmax_t actual_size = 0;
    const auto     actual_hash = sha256_file(entry / kPcmFilename, kMaxPcmBytes, &actual_size);
    if (!actual_hash.has_value() || actual_size != pcm_size || *actual_hash != *pcm_hash) {
        return false;
    }
    return materialize_file(MaterializationPaths{.source = entry / kPcmFilename, .destination = destination}, *pcm_hash, pcm_size, [&] {
        // Revalidate after the potentially long shared-PCM copy and hash,
        // immediately before the atomic local publish. This closes the
        // practical prepare/materialize window; callers still must not
        // mutate compiler inputs concurrently with a build.
        const auto final_bundle = input_bundle(*context, false);
        return final_bundle.has_value() && final_bundle->key == key;
    });
}

void PcmArtifactCache::store(const PcmCacheContext &context, const fs::path &pcm, std::string_view expected_key) const {
    const auto bundle = input_bundle(context, false);
    if (!bundle.has_value() || bundle->key != expected_key) {
        return;
    }
    std::uintmax_t pcm_size = 0;
    const auto     pcm_hash = sha256_file(pcm, kMaxPcmBytes, &pcm_size);
    if (!pcm_hash.has_value()) {
        return;
    }

    std::error_code ec;
    fs::create_directories(directory_, ec);
    if (ec || !regular_non_symlink_directory(directory_)) {
        return;
    }
    const fs::path entry    = directory_ / bundle->key;
    const auto     existing = fs::symlink_status(entry, ec);
    if (!ec && fs::exists(existing)) {
        return;
    }
    if (ec && ec != std::errc::no_such_file_or_directory) {
        return;
    }

    llvm::SmallString<256> temporary_storage;
    if (llvm::sys::fs::createUniqueDirectory((directory_ / ("." + bundle->key + ".tmp.%%%%%%")).string(), temporary_storage)) {
        return;
    }
    const fs::path temporary{temporary_storage.str().str()};
    if (!regular_non_symlink_directory(temporary)) {
        cleanup_temporary_directory(directory_, temporary);
        return;
    }
    const fs::path temporary_pcm = temporary / kPcmFilename;
    fs::copy_file(pcm, temporary_pcm, fs::copy_options::none, ec);
    if (ec) {
        cleanup_temporary_directory(directory_, temporary);
        return;
    }
    std::uintmax_t copied_size = 0;
    const auto     copied_hash = sha256_file(temporary_pcm, kMaxPcmBytes, &copied_size);
    if (!copied_hash.has_value() || copied_size != pcm_size || *copied_hash != *pcm_hash) {
        cleanup_temporary_directory(directory_, temporary);
        return;
    }

    json::Object root;
    root["schema"]                    = std::string(kSchema);
    root["context"]                   = bundle->canonical_context;
    root["cache_key"]                 = bundle->key;
    root["source"]                    = fingerprint_object(bundle->source);
    root["dependencies"]              = fingerprint_array(bundle->dependencies);
    root["external_pcm_dependencies"] = fingerprint_array(bundle->external_pcm_dependencies);
    root["pcm_sha256"]                = *pcm_hash;
    root["pcm_size"]                  = std::to_string(pcm_size);
    std::string              metadata;
    llvm::raw_string_ostream output(metadata);
    output << json::Value(std::move(root));
    output << '\n';
    output.flush();
    if (metadata.size() > kMaxMetadataBytes) {
        cleanup_temporary_directory(directory_, temporary);
        return;
    }
    {
        std::ofstream stream(temporary / kMetadataFilename, std::ios::binary | std::ios::trunc);
        if (!stream) {
            cleanup_temporary_directory(directory_, temporary);
            return;
        }
        stream.write(metadata.data(), static_cast<std::streamsize>(metadata.size()));
        stream.close();
        if (!stream) {
            cleanup_temporary_directory(directory_, temporary);
            return;
        }
    }
    if (!sha256_file(temporary / kMetadataFilename, kMaxMetadataBytes).has_value()) {
        cleanup_temporary_directory(directory_, temporary);
        return;
    }

    // Entries are immutable after this point. Permission changes are
    // best-effort because Windows ACLs and FAT-like filesystems differ.
    fs::permissions(temporary_pcm, fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read, fs::perm_options::replace, ec);
    ec.clear();
    fs::permissions(temporary / kMetadataFilename, fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read,
                    fs::perm_options::replace, ec);
    ec.clear();
    fs::rename(temporary, entry, ec);
    // A concurrent winner (or an unsupported directory rename) is a harmless
    // miss: do not remove or replace either the winner or our temporary tree.
    if (ec) {
        // Files are made read-only before publication so a reader never sees
        // a writable entry. Restore owner-write on our own regular temporary
        // files before recursive cleanup: Windows cannot remove read-only
        // files from a losing concurrent writer's directory.
        const auto restore_owner_write = [](const fs::path &path) {
            std::error_code restore_ec;
            const auto      status = fs::symlink_status(path, restore_ec);
            if (restore_ec || fs::is_symlink(status) || !fs::is_regular_file(status)) {
                return;
            }
            fs::permissions(path, fs::perms::owner_write, fs::perm_options::add, restore_ec);
        };
        restore_owner_write(temporary_pcm);
        restore_owner_write(temporary / kMetadataFilename);
        cleanup_temporary_directory(directory_, temporary);
    }
}

} // namespace gentest::codegen
