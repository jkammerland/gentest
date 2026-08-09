#include "parse_cache.hpp"

#include "mock_manifest.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <llvm/ADT/SmallString.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/JSON.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SHA256.h>
#include <llvm/Support/raw_ostream.h>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

namespace gentest::codegen {
namespace {
namespace fs   = std::filesystem;
namespace json = llvm::json;

constexpr std::string_view kSchema             = "gentest.textual_parse_cache.v1";
constexpr std::uintmax_t   kMaxCacheEntryBytes = std::uintmax_t{64} * 1024U * 1024U;
constexpr std::uintmax_t   kMaxInputBytes      = std::uintmax_t{256} * 1024U * 1024U;
constexpr std::uintmax_t   kMaxExecutableBytes = std::uintmax_t{1024} * 1024U * 1024U;
constexpr std::size_t      kMaxCacheItems      = 200000;

std::string normalize_path(std::string_view raw) {
    if (raw.empty()) {
        return {};
    }
    std::error_code ec;
    fs::path        path{std::string{raw}};
    if (path.is_relative()) {
        if (const fs::path absolute = fs::absolute(path, ec); !ec) {
            path = absolute;
        }
    }
    return path.lexically_normal().generic_string();
}

std::string sha256_hex(std::string_view content) {
    llvm::SHA256 hasher;
    hasher.update(llvm::StringRef{content.data(), content.size()});
    const auto            digest = hasher.final();
    static constexpr char hex[]  = "0123456789abcdef";
    std::string           out;
    out.resize(digest.size() * 2);
    for (std::size_t idx = 0; idx < digest.size(); ++idx) {
        out[idx * 2]     = hex[(digest[idx] >> 4) & 0x0F];
        out[idx * 2 + 1] = hex[digest[idx] & 0x0F];
    }
    return out;
}

struct BoundedFileIdentity {
    std::string hash;
    std::string unique_id;
};

std::optional<BoundedFileIdentity> bounded_file_identity(const fs::path &path, std::uintmax_t max_size, bool allow_empty) {
    auto file = llvm::sys::fs::openNativeFileForRead(path.string());
    if (!file) {
        return std::nullopt;
    }
    int file_descriptor = *file;

    llvm::sys::fs::file_status initial_status;
    if (llvm::sys::fs::status(file_descriptor, initial_status) || !llvm::sys::fs::is_regular_file(initial_status) ||
        initial_status.getSize() > max_size || (!allow_empty && initial_status.getSize() == 0)) {
        [[maybe_unused]] const std::error_code close_error = llvm::sys::fs::closeFile(file_descriptor);
        return std::nullopt;
    }

    llvm::SHA256                               hasher;
    std::array<char, std::size_t{128} * 1024U> chunk{};
    std::uintmax_t                             remaining = initial_status.getSize();
    while (remaining != 0) {
        const std::size_t requested = static_cast<std::size_t>(std::min<std::uintmax_t>(remaining, chunk.size()));
        auto              read      = llvm::sys::fs::readNativeFile(file_descriptor, llvm::MutableArrayRef<char>{chunk.data(), requested});
        if (!read || *read == 0) {
            if (!read) {
                llvm::consumeError(read.takeError());
            }
            [[maybe_unused]] const std::error_code close_error = llvm::sys::fs::closeFile(file_descriptor);
            return std::nullopt;
        }
        hasher.update(llvm::StringRef{chunk.data(), *read});
        remaining -= *read;
    }

    char extra = 0;
    auto eof   = llvm::sys::fs::readNativeFile(file_descriptor, llvm::MutableArrayRef<char>{&extra, 1});
    if (!eof || *eof != 0) {
        if (!eof) {
            llvm::consumeError(eof.takeError());
        }
        [[maybe_unused]] const std::error_code close_error = llvm::sys::fs::closeFile(file_descriptor);
        return std::nullopt;
    }

    llvm::sys::fs::file_status final_descriptor_status;
    llvm::sys::fs::file_status final_path_status;
    const bool                 descriptor_unchanged = !llvm::sys::fs::status(file_descriptor, final_descriptor_status) &&
                                      llvm::sys::fs::is_regular_file(final_descriptor_status) &&
                                      final_descriptor_status.getSize() == initial_status.getSize() &&
                                      final_descriptor_status.getUniqueID() == initial_status.getUniqueID();
    const std::error_code close_error = llvm::sys::fs::closeFile(file_descriptor);
    const bool            path_unchanged =
        !llvm::sys::fs::status(path.string(), final_path_status) && llvm::sys::fs::is_regular_file(final_path_status) &&
        final_path_status.getSize() == initial_status.getSize() && final_path_status.getUniqueID() == initial_status.getUniqueID();
    if (!descriptor_unchanged || !path_unchanged || close_error) {
        return std::nullopt;
    }

    const auto            digest = hasher.final();
    static constexpr char hex[]  = "0123456789abcdef";
    std::string           hash(digest.size() * 2, '\0');
    for (std::size_t idx = 0; idx < digest.size(); ++idx) {
        hash[idx * 2]     = hex[(digest[idx] >> 4) & 0x0F];
        hash[idx * 2 + 1] = hex[digest[idx] & 0x0F];
    }
    const auto unique_id = initial_status.getUniqueID();
    return BoundedFileIdentity{
        .hash      = std::move(hash),
        .unique_id = std::to_string(unique_id.getDevice()) + ":" + std::to_string(unique_id.getFile()),
    };
}

std::string json_text(const json::Value &value) {
    std::string              text;
    llvm::raw_string_ostream output(text);
    output << value;
    output.flush();
    return text;
}

void append_length_prefixed(std::string &out, std::string_view value) {
    out += std::to_string(value.size());
    out.push_back(':');
    out.append(value.data(), value.size());
    out.push_back('\0');
}

void append_fingerprint_material(std::string &out, std::string_view prefix, const TextualParseCache::FileFingerprint &fingerprint) {
    out.push_back('\n');
    append_length_prefixed(out, prefix);
    append_length_prefixed(out, fingerprint.path);
    append_length_prefixed(out, fingerprint.identity);
    append_length_prefixed(out, fingerprint.unique_id);
    append_length_prefixed(out, fingerprint.hash);
    out.push_back(fingerprint.exists ? '1' : '0');
    out.push_back(fingerprint.regular ? '1' : '0');
}

json::Array string_array(const std::vector<std::string> &values) {
    json::Array out;
    out.reserve(values.size());
    for (const auto &value : values) {
        out.push_back(value);
    }
    return out;
}

bool read_string_array(const json::Object &object, llvm::StringRef key, std::vector<std::string> &out) {
    const auto *values = object.getArray(key);
    if (values == nullptr || values->size() > kMaxCacheItems) {
        return false;
    }
    out.clear();
    out.reserve(values->size());
    for (const auto &value : *values) {
        const auto text = value.getAsString();
        if (!text.has_value()) {
            return false;
        }
        out.emplace_back(text->str());
    }
    return true;
}

json::Object fixture_use_object(const FreeFixtureUse &use) {
    return json::Object{
        {.K = "type", .V = use.type_name},
        {.K = "scope", .V = static_cast<std::int64_t>(use.scope)},
        {.K = "suite", .V = use.suite_name},
    };
}

json::Array fixture_use_array(const std::vector<FreeFixtureUse> &uses) {
    json::Array out;
    out.reserve(uses.size());
    for (const auto &use : uses) {
        out.push_back(fixture_use_object(use));
    }
    return out;
}

json::Object call_arg_object(const FreeCallArg &arg) {
    return json::Object{
        {.K = "kind", .V = static_cast<std::int64_t>(arg.kind)},
        {.K = "fixture_index", .V = static_cast<std::int64_t>(arg.fixture_index)},
        {.K = "value", .V = arg.value_expression},
    };
}

json::Array call_arg_array(const std::vector<FreeCallArg> &args) {
    json::Array out;
    out.reserve(args.size());
    for (const auto &arg : args) {
        out.push_back(call_arg_object(arg));
    }
    return out;
}

json::Array optional_scope_array(const std::vector<std::optional<FixtureScope>> &scopes) {
    json::Array out;
    out.reserve(scopes.size());
    for (const auto &scope : scopes) {
        if (scope.has_value()) {
            out.push_back(static_cast<std::int64_t>(*scope));
        } else {
            out.push_back(nullptr);
        }
    }
    return out;
}

json::Object case_object(const TestCaseInfo &test) {
    return json::Object{
        {.K = "qualified_name", .V = test.qualified_name},
        {.K = "display_name", .V = test.display_name},
        {.K = "base_name", .V = test.base_name},
        {.K = "tu_filename", .V = test.tu_filename},
        {.K = "filename", .V = test.filename},
        {.K = "suite_name", .V = test.suite_name},
        {.K = "line", .V = static_cast<std::int64_t>(test.line)},
        {.K = "is_benchmark", .V = test.is_benchmark},
        {.K = "is_jitter", .V = test.is_jitter},
        {.K = "is_baseline", .V = test.is_baseline},
        {.K = "items_per_call", .V = static_cast<std::int64_t>(test.items_per_call)},
        {.K = "is_function_template", .V = test.is_function_template},
        {.K = "returns_value", .V = test.returns_value},
        {.K = "returns_async", .V = test.returns_async},
        {.K = "tags", .V = string_array(test.tags)},
        {.K = "requirements", .V = string_array(test.requirements)},
        {.K = "should_skip", .V = test.should_skip},
        {.K = "skip_reason", .V = test.skip_reason},
        {.K = "fixture_qualified_name", .V = test.fixture_qualified_name},
        {.K = "fixture_lifetime", .V = static_cast<std::int64_t>(test.fixture_lifetime)},
        {.K = "template_args", .V = string_array(test.template_args)},
        {.K = "call_arguments", .V = test.call_arguments},
        {.K = "free_fixture_types", .V = string_array(test.free_fixture_types)},
        {.K = "free_fixture_required_scopes", .V = optional_scope_array(test.free_fixture_required_scopes)},
        {.K = "free_fixtures", .V = fixture_use_array(test.free_fixtures)},
        {.K = "free_call_args", .V = call_arg_array(test.free_call_args)},
        {.K = "namespace_parts", .V = string_array(test.namespace_parts)},
        {.K = "owner", .V = test.owner},
    };
}

json::Array case_array(const std::vector<TestCaseInfo> &cases) {
    json::Array out;
    out.reserve(cases.size());
    for (const auto &test : cases) {
        out.push_back(case_object(test));
    }
    return out;
}

json::Object fixture_object(const FixtureDeclInfo &fixture) {
    return json::Object{
        {.K = "qualified_name", .V = fixture.qualified_name},
        {.K = "base_name", .V = fixture.base_name},
        {.K = "namespace_parts", .V = string_array(fixture.namespace_parts)},
        {.K = "suite_name", .V = fixture.suite_name},
        {.K = "scope", .V = static_cast<std::int64_t>(fixture.scope)},
        {.K = "tu_filename", .V = fixture.tu_filename},
        {.K = "filename", .V = fixture.filename},
        {.K = "line", .V = static_cast<std::int64_t>(fixture.line)},
    };
}

json::Array fixture_array(const std::vector<FixtureDeclInfo> &fixtures) {
    json::Array out;
    out.reserve(fixtures.size());
    for (const auto &fixture : fixtures) {
        out.push_back(fixture_object(fixture));
    }
    return out;
}

bool get_string(const json::Object &object, llvm::StringRef key, std::string &out) {
    const auto value = object.getString(key);
    if (!value.has_value()) {
        return false;
    }
    out = value->str();
    return true;
}

bool get_bool(const json::Object &object, llvm::StringRef key, bool &out) {
    const auto value = object.getBoolean(key);
    if (!value.has_value()) {
        return false;
    }
    out = *value;
    return true;
}

bool get_integer(const json::Object &object, llvm::StringRef key, std::int64_t &out) {
    const auto value = object.getInteger(key);
    if (!value.has_value()) {
        return false;
    }
    out = *value;
    return true;
}

bool read_fixture_uses(const json::Object &object, std::vector<FreeFixtureUse> &out) {
    const auto *values = object.getArray("free_fixtures");
    if (values == nullptr || values->size() > kMaxCacheItems) {
        return false;
    }
    out.clear();
    out.reserve(values->size());
    for (const auto &value : *values) {
        const auto    *entry = value.getAsObject();
        std::int64_t   scope = 0;
        FreeFixtureUse use;
        if (entry == nullptr || !get_string(*entry, "type", use.type_name) || !get_integer(*entry, "scope", scope) ||
            !get_string(*entry, "suite", use.suite_name) || scope < static_cast<std::int64_t>(FixtureScope::Local) ||
            scope > static_cast<std::int64_t>(FixtureScope::Global)) {
            return false;
        }
        use.scope = static_cast<FixtureScope>(scope);
        out.push_back(std::move(use));
    }
    return true;
}

bool read_call_args(const json::Object &object, std::vector<FreeCallArg> &out) {
    const auto *values = object.getArray("free_call_args");
    if (values == nullptr || values->size() > kMaxCacheItems) {
        return false;
    }
    out.clear();
    out.reserve(values->size());
    for (const auto &value : *values) {
        const auto  *entry         = value.getAsObject();
        std::int64_t kind          = 0;
        std::int64_t fixture_index = 0;
        FreeCallArg  arg;
        if (entry == nullptr || !get_integer(*entry, "kind", kind) || !get_integer(*entry, "fixture_index", fixture_index) ||
            !get_string(*entry, "value", arg.value_expression) || kind < static_cast<std::int64_t>(FreeCallArgKind::Fixture) ||
            kind > static_cast<std::int64_t>(FreeCallArgKind::Value) || fixture_index < 0 ||
            static_cast<std::uint64_t>(fixture_index) > std::numeric_limits<std::size_t>::max()) {
            return false;
        }
        arg.kind          = static_cast<FreeCallArgKind>(kind);
        arg.fixture_index = static_cast<std::size_t>(fixture_index);
        out.push_back(std::move(arg));
    }
    return true;
}

bool read_optional_scopes(const json::Object &object, std::vector<std::optional<FixtureScope>> &out) {
    const auto *values = object.getArray("free_fixture_required_scopes");
    if (values == nullptr || values->size() > kMaxCacheItems) {
        return false;
    }
    out.clear();
    out.reserve(values->size());
    for (const auto &value : *values) {
        if (value.getAsNull().has_value()) {
            out.emplace_back(std::nullopt);
            continue;
        }
        const auto scope = value.getAsInteger();
        if (!scope.has_value() || *scope < static_cast<std::int64_t>(FixtureScope::Local) ||
            *scope > static_cast<std::int64_t>(FixtureScope::Global)) {
            return false;
        }
        out.emplace_back(static_cast<FixtureScope>(*scope));
    }
    return true;
}

bool read_cases(const json::Object &object, std::vector<TestCaseInfo> &out) {
    const auto *values = object.getArray("cases");
    if (values == nullptr || values->size() > kMaxCacheItems) {
        return false;
    }
    out.clear();
    out.reserve(values->size());
    for (const auto &value : *values) {
        const auto  *entry = value.getAsObject();
        TestCaseInfo test;
        std::int64_t line           = 0;
        std::int64_t items_per_call = 0;
        std::int64_t lifetime       = 0;
        if (entry == nullptr || !get_string(*entry, "qualified_name", test.qualified_name) ||
            !get_string(*entry, "display_name", test.display_name) || !get_string(*entry, "base_name", test.base_name) ||
            !get_string(*entry, "tu_filename", test.tu_filename) || !get_string(*entry, "filename", test.filename) ||
            !get_string(*entry, "suite_name", test.suite_name) || !get_integer(*entry, "line", line) || line < 0 ||
            std::cmp_greater(line, std::numeric_limits<unsigned>::max()) || !get_bool(*entry, "is_benchmark", test.is_benchmark) ||
            !get_bool(*entry, "is_jitter", test.is_jitter) || !get_bool(*entry, "is_baseline", test.is_baseline) ||
            !get_integer(*entry, "items_per_call", items_per_call) || items_per_call < 0 ||
            !get_bool(*entry, "is_function_template", test.is_function_template) ||
            !get_bool(*entry, "returns_value", test.returns_value) || !get_bool(*entry, "returns_async", test.returns_async) ||
            !read_string_array(*entry, "tags", test.tags) || !read_string_array(*entry, "requirements", test.requirements) ||
            !get_bool(*entry, "should_skip", test.should_skip) || !get_string(*entry, "skip_reason", test.skip_reason) ||
            !get_string(*entry, "fixture_qualified_name", test.fixture_qualified_name) ||
            !get_integer(*entry, "fixture_lifetime", lifetime) || lifetime < static_cast<std::int64_t>(FixtureLifetime::None) ||
            lifetime > static_cast<std::int64_t>(FixtureLifetime::MemberGlobal) ||
            !read_string_array(*entry, "template_args", test.template_args) || !get_string(*entry, "call_arguments", test.call_arguments) ||
            !read_string_array(*entry, "free_fixture_types", test.free_fixture_types) ||
            !read_optional_scopes(*entry, test.free_fixture_required_scopes) || !read_fixture_uses(*entry, test.free_fixtures) ||
            !read_call_args(*entry, test.free_call_args) || !read_string_array(*entry, "namespace_parts", test.namespace_parts) ||
            !get_string(*entry, "owner", test.owner)) {
            return false;
        }
        test.line             = static_cast<unsigned>(line);
        test.items_per_call   = static_cast<std::uint64_t>(items_per_call);
        test.fixture_lifetime = static_cast<FixtureLifetime>(lifetime);
        if (test.free_fixture_types.size() != test.free_fixture_required_scopes.size()) {
            return false;
        }
        for (const auto &arg : test.free_call_args) {
            // Per-TU results are cached before the target-wide fixture
            // resolution pass. At that point free_fixtures is intentionally
            // empty and fixture call arguments refer to free_fixture_types.
            // A result that has already been resolved instead refers to its
            // concrete free_fixtures. Validate whichever representation is
            // present, so corrupt entries remain a miss without rejecting
            // valid cached pre-resolution data.
            const std::size_t fixture_count = test.free_fixtures.empty() ? test.free_fixture_types.size() : test.free_fixtures.size();
            if (arg.kind == FreeCallArgKind::Fixture && arg.fixture_index >= fixture_count) {
                return false;
            }
        }
        out.push_back(std::move(test));
    }
    return true;
}

bool read_fixtures(const json::Object &object, std::vector<FixtureDeclInfo> &out) {
    const auto *values = object.getArray("fixtures");
    if (values == nullptr || values->size() > kMaxCacheItems) {
        return false;
    }
    out.clear();
    out.reserve(values->size());
    for (const auto &value : *values) {
        const auto     *entry = value.getAsObject();
        FixtureDeclInfo fixture;
        std::int64_t    scope = 0;
        std::int64_t    line  = 0;
        if (entry == nullptr || !get_string(*entry, "qualified_name", fixture.qualified_name) ||
            !get_string(*entry, "base_name", fixture.base_name) || !read_string_array(*entry, "namespace_parts", fixture.namespace_parts) ||
            !get_string(*entry, "suite_name", fixture.suite_name) || !get_integer(*entry, "scope", scope) ||
            scope < static_cast<std::int64_t>(FixtureScope::Local) || scope > static_cast<std::int64_t>(FixtureScope::Global) ||
            !get_string(*entry, "tu_filename", fixture.tu_filename) || !get_string(*entry, "filename", fixture.filename) ||
            !get_integer(*entry, "line", line) || line < 0 || std::cmp_greater(line, std::numeric_limits<unsigned>::max())) {
            return false;
        }
        fixture.scope = static_cast<FixtureScope>(scope);
        fixture.line  = static_cast<unsigned>(line);
        out.push_back(std::move(fixture));
    }
    return true;
}

json::Object fingerprint_object(const TextualParseCache::FileFingerprint &fingerprint) {
    return json::Object{
        {.K = "path", .V = fingerprint.path}, {.K = "identity", .V = fingerprint.identity}, {.K = "unique_id", .V = fingerprint.unique_id},
        {.K = "hash", .V = fingerprint.hash}, {.K = "exists", .V = fingerprint.exists},     {.K = "regular", .V = fingerprint.regular},
    };
}

json::Array fingerprint_array(const std::vector<TextualParseCache::FileFingerprint> &fingerprints) {
    json::Array out;
    out.reserve(fingerprints.size());
    for (const auto &fingerprint : fingerprints) {
        out.push_back(fingerprint_object(fingerprint));
    }
    return out;
}

bool read_fingerprint(const json::Value &value, TextualParseCache::FileFingerprint &out) {
    const auto *object = value.getAsObject();
    return object != nullptr && get_string(*object, "path", out.path) && get_string(*object, "identity", out.identity) &&
           get_string(*object, "unique_id", out.unique_id) && get_string(*object, "hash", out.hash) &&
           get_bool(*object, "exists", out.exists) && get_bool(*object, "regular", out.regular);
}

bool read_fingerprints(const json::Object &object, llvm::StringRef key, std::vector<TextualParseCache::FileFingerprint> &out) {
    const auto *values = object.getArray(key);
    if (values == nullptr || values->size() > kMaxCacheItems) {
        return false;
    }
    out.clear();
    out.reserve(values->size());
    for (const auto &value : *values) {
        TextualParseCache::FileFingerprint fingerprint;
        if (!read_fingerprint(value, fingerprint)) {
            return false;
        }
        out.push_back(std::move(fingerprint));
    }
    return true;
}

bool write_atomic_best_effort(const fs::path &path, std::string_view content) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
        return false;
    }
    const auto directory_status = fs::symlink_status(path.parent_path(), ec);
    if (ec || fs::is_symlink(directory_status) || !fs::is_directory(directory_status)) {
        return false;
    }

    llvm::SmallString<256> temporary_path;
    int                    temporary_fd = -1;
    const std::string      model        = (path.parent_path() / ("." + path.filename().string() + ".%%%%%%%%")).string();
    if (llvm::sys::fs::createUniqueFile(model, temporary_fd, temporary_path) || temporary_fd < 0) {
        return false;
    }
    llvm::raw_fd_ostream output(temporary_fd, /*shouldClose=*/true);
    output.write(content.data(), content.size());
    output.close();
    if (output.has_error()) {
        std::error_code ignored;
        fs::remove(temporary_path.str().str(), ignored);
        return false;
    }

    const fs::path temporary{temporary_path.str().str()};
#if defined(_WIN32)
    // Windows rename does not replace an existing destination. The cache slot
    // is a hash-derived leaf under the already-validated cache directory, so
    // removing only a regular entry preserves best-effort last-writer-wins
    // behavior without following a hostile symlink.
    const auto destination_status = fs::symlink_status(path, ec);
    if (!ec && fs::exists(destination_status)) {
        if (!fs::is_regular_file(destination_status) || fs::is_symlink(destination_status)) {
            std::error_code ignored;
            fs::remove(temporary, ignored);
            return false;
        }
        if (fs::remove(path, ec) || ec) {
            std::error_code ignored;
            fs::remove(temporary, ignored);
            return false;
        }
    } else if (ec && ec != std::make_error_code(std::errc::no_such_file_or_directory)) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        return false;
    }
    ec.clear();
#endif
    fs::rename(temporary, path, ec);
    if (ec) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        return false;
    }
    return true;
}

std::string canonical_context_text(const ParseCacheContext &context) {
    std::string text;
    append_length_prefixed(text, kSchema);
    append_length_prefixed(text, context.tool_identity);
    append_length_prefixed(text, context.salt);
    append_length_prefixed(text, context.parse_policy);
    append_length_prefixed(text, normalize_path(context.source));
    append_length_prefixed(text, context.adjusted_command);
    append_length_prefixed(text, normalize_path(context.working_directory));
    for (const auto &root : context.include_roots) {
        append_length_prefixed(text, root);
    }
    return text;
}

} // namespace

TextualParseCache::TextualParseCache(fs::path directory) : directory_(std::move(directory)) {}

std::string TextualParseCache::executable_identity(const fs::path &path) {
    std::error_code ec;
    const fs::path  resolved = fs::weakly_canonical(path, ec);
    if (ec) {
        return {};
    }
    const auto identity = bounded_file_identity(resolved, kMaxExecutableBytes, false);
    if (!identity.has_value()) {
        return {};
    }
    return identity->hash;
}

std::optional<TextualParseCache::FileFingerprint> TextualParseCache::fingerprint_file(const std::string &raw_path, bool allow_memoization) {
    const std::string path = normalize_path(raw_path);
    if (path.empty()) {
        return std::nullopt;
    }
    if (allow_memoization) {
        std::lock_guard lock(mutex_);
        if (const auto it = file_fingerprints_.find(path); it != file_fingerprints_.end()) {
            return it->second;
        }
    }

    FileFingerprint fingerprint;
    fingerprint.path = path;
    std::error_code ec;
    const auto      status = fs::status(path, ec);
    if (ec || !fs::exists(status)) {
        if (ec && ec != std::make_error_code(std::errc::no_such_file_or_directory)) {
            return std::nullopt;
        }
    } else {
        fingerprint.exists  = true;
        fingerprint.regular = fs::is_regular_file(status);
        if (const fs::path canonical = fs::weakly_canonical(path, ec); !ec) {
            fingerprint.identity = canonical.generic_string();
        } else {
            return std::nullopt;
        }
        if (fingerprint.regular) {
            const auto file_identity = bounded_file_identity(path, kMaxInputBytes, true);
            if (!file_identity.has_value()) {
                return std::nullopt;
            }
            fingerprint.unique_id = file_identity->unique_id;
            fingerprint.hash      = file_identity->hash;

            ec.clear();
            const fs::path final_canonical = fs::weakly_canonical(path, ec);
            if (ec || final_canonical.generic_string() != fingerprint.identity) {
                return std::nullopt;
            }
        } else {
            llvm::sys::fs::file_status llvm_status;
            if (llvm::sys::fs::status(path, llvm_status)) {
                return std::nullopt;
            }
            const auto unique_id  = llvm_status.getUniqueID();
            fingerprint.unique_id = std::to_string(unique_id.getDevice()) + ":" + std::to_string(unique_id.getFile());
        }
    }
    if (!fingerprint.exists) {
        ec.clear();
        if (const fs::path canonical = fs::weakly_canonical(path, ec); !ec) {
            fingerprint.identity = canonical.generic_string();
        } else {
            return std::nullopt;
        }
    }
    if (!allow_memoization) {
        return fingerprint;
    }
    std::lock_guard lock(mutex_);
    auto [it, _] = file_fingerprints_.emplace(path, std::move(fingerprint));
    return it->second;
}

std::string TextualParseCache::slot_for(const ParseCacheContext &context) const { return sha256_hex(canonical_context_text(context)); }

bool TextualParseCache::fingerprint_matches(const FileFingerprint &expected) {
    // Cache-entry validation must always observe the filesystem as it exists
    // at this load. Store-side memoization only avoids duplicate hashing while
    // serializing results from one parse invocation.
    const auto actual = fingerprint_file(expected.path, false);
    return actual.has_value() && actual->identity == expected.identity && actual->unique_id == expected.unique_id &&
           actual->exists == expected.exists && actual->regular == expected.regular && actual->hash == expected.hash;
}

bool TextualParseCache::load(const ParseCacheContext &context, TextualParseResult &result) {
    if (!enabled() || context.tool_identity.empty()) {
        return false;
    }
    std::error_code directory_ec;
    const auto      directory_status = fs::symlink_status(directory_, directory_ec);
    if (directory_ec || fs::is_symlink(directory_status) || !fs::is_directory(directory_status)) {
        return false;
    }
    const fs::path  entry_path = directory_ / (slot_for(context) + ".json");
    std::error_code file_ec;
    const auto      entry_link_status = fs::symlink_status(entry_path, file_ec);
    if (file_ec || fs::is_symlink(entry_link_status)) {
        return false;
    }
    auto entry_file = llvm::sys::fs::openNativeFileForRead(entry_path.string());
    if (!entry_file) {
        return false;
    }
    auto                       entry_fd = *entry_file;
    llvm::sys::fs::file_status entry_status;
    if (llvm::sys::fs::status(entry_fd, entry_status) || !llvm::sys::fs::is_regular_file(entry_status) ||
        entry_status.getSize() > kMaxCacheEntryBytes) {
        if (llvm::sys::fs::closeFile(entry_fd)) {
            return false;
        }
        return false;
    }
    auto buffer = llvm::MemoryBuffer::getOpenFile(entry_fd, entry_path.string(), entry_status.getSize(),
                                                  /*RequiresNullTerminator=*/true, /*IsVolatile=*/true);
    if (llvm::sys::fs::closeFile(entry_fd)) {
        return false;
    }
    if (!buffer) {
        return false;
    }
    auto parsed = json::parse((*buffer)->getBuffer());
    if (!parsed) {
        return false;
    }
    const auto *root = parsed->getAsObject();
    std::string schema;
    std::string stored_context;
    std::string cache_key;
    if (root == nullptr || !get_string(*root, "schema", schema) || schema != kSchema || !get_string(*root, "context", stored_context) ||
        stored_context != canonical_context_text(context) || !get_string(*root, "cache_key", cache_key)) {
        return false;
    }

    FileFingerprint              source;
    std::vector<FileFingerprint> dependencies;
    std::vector<FileFingerprint> shadows;
    std::vector<FileFingerprint> command_inputs;
    if (const auto *source_value = root->get("source");
        source_value == nullptr || !read_fingerprint(*source_value, source) || !read_fingerprints(*root, "dependencies", dependencies) ||
        !read_fingerprints(*root, "shadow_guards", shadows) || !read_fingerprints(*root, "command_input_guards", command_inputs) ||
        !fingerprint_matches(source)) {
        return false;
    }
    for (const auto &fingerprint : dependencies) {
        if (!fingerprint_matches(fingerprint)) {
            return false;
        }
    }
    for (const auto &fingerprint : shadows) {
        if (!fingerprint_matches(fingerprint)) {
            return false;
        }
    }
    for (const auto &fingerprint : command_inputs) {
        if (!fingerprint_matches(fingerprint)) {
            return false;
        }
    }

    std::string material = canonical_context_text(context);
    append_fingerprint_material(material, "source", source);
    for (const auto &fingerprint : dependencies) {
        append_fingerprint_material(material, "dep", fingerprint);
    }
    for (const auto &fingerprint : shadows) {
        append_fingerprint_material(material, "shadow", fingerprint);
    }
    for (const auto &fingerprint : command_inputs) {
        append_fingerprint_material(material, "command-input", fingerprint);
    }
    if (sha256_hex(material) != cache_key) {
        return false;
    }

    std::string result_checksum;
    const auto *parsed_result_value = root->get("result");
    if (!get_string(*root, "result_checksum", result_checksum) || parsed_result_value == nullptr ||
        sha256_hex(json_text(*parsed_result_value)) != result_checksum) {
        return false;
    }

    const auto  *parsed_result = parsed_result_value->getAsObject();
    std::int64_t status        = 0;
    std::string  mocks_manifest;
    if (parsed_result == nullptr || !get_integer(*parsed_result, "status", status) ||
        !get_bool(*parsed_result, "had_test_errors", result.had_test_errors) ||
        !get_bool(*parsed_result, "had_fixture_errors", result.had_fixture_errors) ||
        !get_bool(*parsed_result, "had_mock_errors", result.had_mock_errors) || !read_cases(*parsed_result, result.cases) ||
        !read_fixtures(*parsed_result, result.fixtures) || !get_string(*parsed_result, "mocks_manifest", mocks_manifest) ||
        !get_string(*parsed_result, "diagnostics", result.diagnostics) || status != 0) {
        return false;
    }
    auto mocks = mock_manifest::parse(mocks_manifest);
    if (!mocks.error.empty()) {
        return false;
    }
    result.status = static_cast<int>(status);
    result.mocks  = std::move(mocks.mocks);
    result.dependencies.clear();
    result.dependencies.reserve(dependencies.size());
    for (const auto &dependency : dependencies) {
        if (!dependency.exists || !dependency.regular) {
            return false;
        }
        result.dependencies.push_back(dependency.path);
    }
    result.shadow_guards.clear();
    result.shadow_guards.reserve(shadows.size());
    for (const auto &shadow : shadows) {
        result.shadow_guards.push_back(shadow.path);
    }
    result.command_input_guards.clear();
    result.command_input_guards.reserve(command_inputs.size());
    for (const auto &input : command_inputs) {
        result.command_input_guards.push_back(input.path);
    }
    result.cacheable = true;
    return true;
}

void TextualParseCache::store(const ParseCacheContext &context, const TextualParseResult &result) {
    if (!enabled() || context.tool_identity.empty() || !result.cacheable || result.status != 0 || result.had_test_errors ||
        result.had_fixture_errors || result.had_mock_errors) {
        return;
    }
    if (result.cases.size() > kMaxCacheItems || result.fixtures.size() > kMaxCacheItems || result.mocks.size() > kMaxCacheItems ||
        result.dependencies.size() > kMaxCacheItems || result.shadow_guards.size() > kMaxCacheItems ||
        result.command_input_guards.size() > kMaxCacheItems || result.parse_input_snapshots.size() > kMaxCacheItems ||
        result.parse_lookup_snapshots.size() > kMaxCacheItems) {
        return;
    }
    if (std::ranges::any_of(result.cases, [](const TestCaseInfo &test) {
            return test.items_per_call > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
                   std::ranges::any_of(test.free_call_args, [](const FreeCallArg &arg) {
                       return static_cast<std::uint64_t>(arg.fixture_index) >
                              static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
                   });
        })) {
        return;
    }
    // Re-read every publication input instead of using the invocation memo.
    // The result was produced from the buffers in parse_input_snapshots; a
    // concurrent edit between parsing and publication must make this store a
    // safe miss rather than associate the old result with new input bytes.
    const auto source = fingerprint_file(context.source, false);
    if (!source.has_value() || !source->exists || !source->regular) {
        return;
    }
    auto collect = [&](const std::vector<std::string> &paths, bool require_regular) {
        std::vector<FileFingerprint> fingerprints;
        fingerprints.reserve(paths.size());
        for (const auto &path : paths) {
            const auto fingerprint = fingerprint_file(path, false);
            if (!fingerprint.has_value() || (require_regular && (!fingerprint->exists || !fingerprint->regular))) {
                return std::optional<std::vector<FileFingerprint>>{};
            }
            fingerprints.push_back(*fingerprint);
        }
        std::ranges::sort(fingerprints, {}, &FileFingerprint::path);
        fingerprints.erase(std::ranges::unique(fingerprints, {}, &FileFingerprint::path).begin(), fingerprints.end());
        return std::optional<std::vector<FileFingerprint>>{std::move(fingerprints)};
    };
    const auto dependencies   = collect(result.dependencies, true);
    const auto shadows        = collect(result.shadow_guards, false);
    const auto command_inputs = collect(result.command_input_guards, true);
    if (!dependencies.has_value() || !shadows.has_value() || !command_inputs.has_value()) {
        return;
    }

    std::unordered_map<std::string, ParseInputSnapshot> parse_inputs;
    parse_inputs.reserve(result.parse_input_snapshots.size());
    for (const auto &snapshot : result.parse_input_snapshots) {
        ParseInputSnapshot normalized = snapshot;
        normalized.path               = normalize_path(snapshot.path);
        if (normalized.path.empty() || normalized.hash.empty() || normalized.unique_id.empty()) {
            return;
        }
        const auto [it, inserted] = parse_inputs.emplace(normalized.path, std::move(normalized));
        if (!inserted && (it->second.hash != snapshot.hash || it->second.unique_id != snapshot.unique_id)) {
            return;
        }
    }
    const auto matches_parse_input = [&](const FileFingerprint &fingerprint) {
        const auto it = parse_inputs.find(fingerprint.path);
        return it != parse_inputs.end() && it->second.hash == fingerprint.hash && it->second.unique_id == fingerprint.unique_id;
    };
    if (!matches_parse_input(*source) ||
        !std::ranges::all_of(*dependencies, [&](const FileFingerprint &fingerprint) { return matches_parse_input(fingerprint); })) {
        return;
    }
    std::unordered_map<std::string, bool> lookup_states;
    lookup_states.reserve(result.parse_lookup_snapshots.size());
    for (const auto &snapshot : result.parse_lookup_snapshots) {
        const std::string path = normalize_path(snapshot.path);
        if (path.empty()) {
            return;
        }
        const auto [it, inserted] = lookup_states.emplace(path, snapshot.exists);
        if (!inserted && it->second != snapshot.exists) {
            return;
        }
    }
    if (!std::ranges::all_of(*shadows, [&](const FileFingerprint &fingerprint) {
            const auto it = lookup_states.find(fingerprint.path);
            return it != lookup_states.end() && it->second == fingerprint.exists && (!fingerprint.exists || fingerprint.regular);
        })) {
        return;
    }

    std::string material = canonical_context_text(context);
    append_fingerprint_material(material, "source", *source);
    const auto append_fingerprints = [&](std::string_view prefix, const std::vector<FileFingerprint> &fingerprints) {
        for (const auto &fingerprint : fingerprints) {
            append_fingerprint_material(material, prefix, fingerprint);
        }
    };
    append_fingerprints("dep", *dependencies);
    append_fingerprints("shadow", *shadows);
    append_fingerprints("command-input", *command_inputs);

    json::Object serialized_result;
    serialized_result["status"]             = static_cast<std::int64_t>(result.status);
    serialized_result["had_test_errors"]    = result.had_test_errors;
    serialized_result["had_fixture_errors"] = result.had_fixture_errors;
    serialized_result["had_mock_errors"]    = result.had_mock_errors;
    serialized_result["cases"]              = case_array(result.cases);
    serialized_result["fixtures"]           = fixture_array(result.fixtures);
    serialized_result["mocks_manifest"]     = mock_manifest::serialize(result.mocks);
    serialized_result["diagnostics"]        = result.diagnostics;

    // cache_key authenticates lookup inputs. Keep a separate checksum for the
    // serialized parse result so a syntactically valid but modified entry is
    // still a miss instead of changing generated output.
    json::Value       serialized_result_value(std::move(serialized_result));
    const std::string result_checksum = sha256_hex(json_text(serialized_result_value));

    json::Object root;
    root["schema"]               = std::string(kSchema);
    root["context"]              = canonical_context_text(context);
    root["cache_key"]            = sha256_hex(material);
    root["source"]               = fingerprint_object(*source);
    root["dependencies"]         = fingerprint_array(*dependencies);
    root["shadow_guards"]        = fingerprint_array(*shadows);
    root["command_input_guards"] = fingerprint_array(*command_inputs);
    root["result_checksum"]      = result_checksum;
    root["result"]               = std::move(serialized_result_value);
    std::string              content;
    llvm::raw_string_ostream output(content);
    output << json::Value(std::move(root));
    output << '\n';
    output.flush();
    if (content.size() > kMaxCacheEntryBytes) {
        return;
    }
    (void)write_atomic_best_effort(directory_ / (slot_for(context) + ".json"), content);
}

} // namespace gentest::codegen
