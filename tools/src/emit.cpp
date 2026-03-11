// Implementation of template-based emission for test cases

#include "emit.hpp"

#include "log.hpp"
#include "parallel_for.hpp"
#include "render.hpp"
#include "render_mocks.hpp"
#include "templates.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace gentest::codegen {

namespace {

namespace fs = std::filesystem;

bool ensure_dir(const fs::path &dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        log_err("gentest_codegen: failed to create directory '{}': {}\n", dir.string(), ec.message());
        return false;
    }
    return true;
}

std::string normalize_path_key(const fs::path &path) {
    std::error_code ec;
    fs::path        abs = fs::absolute(path, ec);
    if (ec) {
        abs = path;
        ec.clear();
    }
    fs::path canon = fs::weakly_canonical(abs, ec);
    if (!ec) abs = canon;
    abs = abs.lexically_normal();
    std::string key = abs.generic_string();
#if defined(_WIN32)
    for (auto &ch : key) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
#endif
    return key;
}

std::string casefolded_path_key(const fs::path &path) {
    std::string key = normalize_path_key(path);
    for (auto &ch : key) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return key;
}

std::string sanitize_stem(std::string value) {
    for (auto &ch : value) {
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_';
        if (!ok) ch = '_';
    }
    if (value.empty())
        return {"tu"};
    return value;
}

bool path_is_under(const fs::path &path, const fs::path &root) {
    auto pit = path.begin();
    auto rit = root.begin();
    for (; rit != root.end(); ++rit, ++pit) {
        if (pit == path.end() || *pit != *rit) {
            return false;
        }
    }
    return true;
}

std::string normalize_case_file(const CollectorOptions &opts, std::string_view filename) {
    if (!opts.source_root || opts.source_root->empty()) {
        return std::string(filename);
    }
    fs::path file_path{std::string(filename)};
    fs::path root_path{*opts.source_root};

    std::error_code ec;
    fs::path        abs_file = file_path.is_absolute() ? file_path : fs::absolute(file_path, ec);
    ec.clear();
    fs::path abs_root = root_path.is_absolute() ? root_path : fs::absolute(root_path, ec);

    ec.clear();
    if (auto canon = fs::weakly_canonical(abs_file, ec); !ec) abs_file = canon;
    ec.clear();
    if (auto canon = fs::weakly_canonical(abs_root, ec); !ec) abs_root = canon;

    abs_file = abs_file.lexically_normal();
    abs_root = abs_root.lexically_normal();

    auto to_lower = [](std::string value) {
#if defined(_WIN32)
        for (auto &ch : value) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
#endif
        return value;
    };

#if defined(_WIN32)
    const fs::path abs_file_ci = fs::path(to_lower(abs_file.generic_string())).lexically_normal();
    const fs::path abs_root_ci = fs::path(to_lower(abs_root.generic_string())).lexically_normal();
    if (path_is_under(abs_file_ci, abs_root_ci)) {
        fs::path rel = abs_file_ci.lexically_relative(abs_root_ci);
        if (!rel.empty()) {
            return rel.generic_string();
        }
    }
#else
    (void)to_lower;
    if (path_is_under(abs_file, abs_root)) {
        fs::path rel = abs_file.lexically_relative(abs_root);
        if (!rel.empty()) {
            return rel.generic_string();
        }
    }
#endif
    return file_path.generic_string();
}

std::optional<std::uint32_t> parse_tu_index(std::string_view filename) {
    // Only accept generated TU wrapper names that start with "tu_".
    // Parsing interior "tu_####" fragments from arbitrary basenames can
    // produce duplicate register symbol names for different source files.
    if (!filename.starts_with("tu_")) {
        return std::nullopt;
    }
    std::size_t i = 3;
    std::uint32_t value = 0;
    std::size_t digits = 0;
    while (i < filename.size()) {
        const char ch = filename[i];
        if (ch < '0' || ch > '9') {
            break;
        }
        value = value * 10u + static_cast<std::uint32_t>(ch - '0');
        ++digits;
        ++i;
    }
    if (digits == 0) {
        return std::nullopt;
    }
    return value;
}

fs::path resolve_tu_header_output(const CollectorOptions &opts, std::size_t idx) {
    if (idx < opts.tu_output_headers.size() && !opts.tu_output_headers[idx].empty()) {
        return opts.tu_output_headers[idx];
    }

    fs::path header_out = opts.tu_output_dir / fs::path(opts.sources[idx]).filename();
    header_out.replace_extension(".h");
    return header_out;
}

bool is_module_interface_source(const fs::path &path) {
    std::string ext = path.extension().string();
    std::ranges::transform(ext, ext.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return ext == ".cppm" || ext == ".ccm" || ext == ".cxxm" || ext == ".ixx" || ext == ".mxx";
}

fs::path resolve_module_wrapper_output(const CollectorOptions &opts, std::size_t idx) {
    fs::path     out = opts.tu_output_dir;
    std::string  stem = sanitize_stem(fs::path(opts.sources[idx]).stem().string());
    const auto   ext = fs::path(opts.sources[idx]).extension().string();
    out /= fmt::format("tu_{:04d}_{}.module.gentest{}", static_cast<unsigned>(idx), stem, ext);
    return out;
}

bool read_file(const fs::path &path, std::string &out);

std::string strip_comments_for_line_scan(std::string_view line, bool &in_block_comment) {
    std::string out;
    out.reserve(line.size());

    for (std::size_t i = 0; i < line.size();) {
        if (in_block_comment) {
            const auto end = line.find("*/", i);
            if (end == std::string_view::npos) {
                return out;
            }
            in_block_comment = false;
            i = end + 2;
            continue;
        }
        if (i + 1 < line.size() && line[i] == '/' && line[i + 1] == '*') {
            in_block_comment = true;
            i += 2;
            continue;
        }
        if (i + 1 < line.size() && line[i] == '/' && line[i + 1] == '/') {
            break;
        }
        out.push_back(line[i]);
        ++i;
    }
    return out;
}

struct SourceMockCodegenIncludes {
    bool has_mock_codegen = false;
    bool has_registry_codegen = false;
    bool has_impl_codegen = false;

    [[nodiscard]] bool has_complete_manual_codegen() const { return has_mock_codegen || (has_registry_codegen && has_impl_codegen); }
};

SourceMockCodegenIncludes scan_source_mock_codegen_includes(std::string_view text) {
    SourceMockCodegenIncludes includes;
    bool                     in_block_comment = false;
    std::size_t              cursor = 0;

    while (cursor < text.size()) {
        const std::size_t line_end = text.find('\n', cursor);
        const std::size_t next = line_end == std::string_view::npos ? text.size() : line_end + 1;
        std::string_view   line = text.substr(cursor, next - cursor);
        if (!line.empty() && line.back() == '\n') {
            line.remove_suffix(1);
        }
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }

        std::string cleaned = strip_comments_for_line_scan(line, in_block_comment);
        const auto  first = cleaned.find_first_not_of(" \t");
        if (first != std::string::npos) {
            cleaned.erase(0, first);
        } else {
            cleaned.clear();
        }
        if (cleaned.rfind("#include", 0) == 0) {
            const auto quote = cleaned.find('"');
            const auto angle = cleaned.find('<');
            std::size_t begin = std::string::npos;
            char        end_delim = '\0';
            if (quote != std::string::npos && (angle == std::string::npos || quote < angle)) {
                begin = quote + 1;
                end_delim = '"';
            } else if (angle != std::string::npos) {
                begin = angle + 1;
                end_delim = '>';
            }
            if (begin != std::string::npos) {
                const auto end = cleaned.find(end_delim, begin);
                if (end != std::string::npos) {
                    const std::string_view header(cleaned.data() + begin, end - begin);
                    includes.has_mock_codegen = includes.has_mock_codegen || header == "gentest/mock_codegen.h";
                    includes.has_registry_codegen = includes.has_registry_codegen || header == "gentest/mock_registry_codegen.h";
                    includes.has_impl_codegen = includes.has_impl_codegen || header == "gentest/mock_impl_codegen.h";
                }
            }
        }
        cursor = next;
    }

    return includes;
}

std::optional<std::size_t> find_module_mock_codegen_include_offset(std::string_view text) {
    bool        seen_module_decl = false;
    bool        in_block_comment = false;
    std::size_t cursor = 0;
    std::size_t last_after = 0;

    while (cursor < text.size()) {
        const std::size_t line_end = text.find('\n', cursor);
        const std::size_t next = line_end == std::string_view::npos ? text.size() : line_end + 1;
        std::string_view   line = text.substr(cursor, next - cursor);
        if (!line.empty() && line.back() == '\n') {
            line.remove_suffix(1);
        }
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        const std::string cleaned = strip_comments_for_line_scan(line, in_block_comment);
        const auto        first = cleaned.find_first_not_of(" \t");
        const auto        last = cleaned.find_last_not_of(" \t");
        const std::string_view trimmed = first == std::string::npos ? std::string_view{} : std::string_view(cleaned).substr(first, last - first + 1);

        if (!seen_module_decl) {
            if (trimmed == "module;") {
                cursor = next;
                continue;
            }
            if (trimmed.starts_with("export module ") || trimmed.starts_with("module ")) {
                seen_module_decl = true;
                last_after = next;
            }
            cursor = next;
            continue;
        }

        if (trimmed.empty() || trimmed.starts_with("import ") || trimmed.starts_with("export import ") || trimmed.starts_with("#")) {
            last_after = next;
            cursor = next;
            continue;
        }
        break;
    }

    if (!seen_module_decl) {
        return std::nullopt;
    }
    return last_after;
}

std::string render_module_mock_codegen_include_block() {
    std::string out;
    out.reserve(192);
    out.append("\n// gentest_codegen: injected mock codegen include.\n");
    out.append("#if defined(__clang__)\n");
    out.append("#pragma clang diagnostic push\n");
    out.append("#pragma clang diagnostic ignored \"-Winclude-angled-in-module-purview\"\n");
    out.append("#endif\n");
    out.append("#include \"gentest/mock_codegen.h\"\n");
    out.append("#if defined(__clang__)\n");
    out.append("#pragma clang diagnostic pop\n");
    out.append("#endif\n");
    return out;
}

bool namespace_chains_equal(const std::vector<MockNamespaceScopeInfo> &lhs, const std::vector<MockNamespaceScopeInfo> &rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        if (lhs[i].name != rhs[i].name || lhs[i].is_inline != rhs[i].is_inline || lhs[i].is_exported != rhs[i].is_exported) {
            return false;
        }
    }
    return true;
}

std::string render_namespace_scope_close(const std::vector<MockNamespaceScopeInfo> &chain) {
    std::string out;
    out.reserve(chain.size() * 32);
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        fmt::format_to(std::back_inserter(out), "\n}} // namespace {}\n", it->name);
    }
    return out;
}

std::string render_namespace_scope_reopen(const std::vector<MockNamespaceScopeInfo> &chain) {
    std::string out;
    out.reserve(chain.size() * 40);
    for (const auto &ns : chain) {
        if (ns.is_exported) {
            out += "export ";
        }
        if (ns.is_inline) {
            out += "inline ";
        }
        fmt::format_to(std::back_inserter(out), "namespace {} {{\n", ns.name);
    }
    return out;
}

std::optional<std::string> render_module_wrapper_source(const fs::path &source_path, const std::vector<const MockClassInfo *> &source_mocks,
                                                        bool needs_mock_codegen_include) {
    std::string original;
    if (!read_file(source_path, original)) {
        log_err("gentest_codegen: failed to read module source '{}'\n", source_path.string());
        return std::nullopt;
    }

    const SourceMockCodegenIncludes manual_includes = scan_source_mock_codegen_includes(original);
    if (source_mocks.empty() && !needs_mock_codegen_include) {
        return original;
    }

    std::map<std::size_t, std::vector<const MockClassInfo *>> mocks_by_offset;
    std::size_t                                                reserve_extra = 0;
    for (const auto *mock : source_mocks) {
        if (mock == nullptr) {
            continue;
        }
        if (!mock->attachment_insertion_offset.has_value()) {
            log_err("gentest_codegen: missing module attachment insertion offset for '{}'\n", mock->qualified_name);
            return std::nullopt;
        }
        const std::size_t offset = *mock->attachment_insertion_offset;
        if (offset > original.size()) {
            log_err("gentest_codegen: invalid module attachment insertion offset {} for '{}'\n", offset, mock->qualified_name);
            return std::nullopt;
        }
        mocks_by_offset[offset].push_back(mock);
        reserve_extra += mock->qualified_name.size() * 4 + 512;
    }
    std::optional<std::size_t> mock_codegen_include_offset;
    if (needs_mock_codegen_include && !manual_includes.has_complete_manual_codegen()) {
        mock_codegen_include_offset = find_module_mock_codegen_include_offset(original);
        if (!mock_codegen_include_offset.has_value()) {
            log_err("gentest_codegen: failed to locate module mock include insertion point in '{}'\n", source_path.string());
            return std::nullopt;
        }
        reserve_extra += 256;
    }

    if (mocks_by_offset.empty() && !mock_codegen_include_offset.has_value()) {
        return original;
    }

    std::string rendered;
    rendered.reserve(original.size() + reserve_extra);

    std::size_t cursor = 0;
    for (auto &[offset, group] : mocks_by_offset) {
        if (mock_codegen_include_offset.has_value() && *mock_codegen_include_offset <= offset) {
            if (*mock_codegen_include_offset < cursor || *mock_codegen_include_offset > original.size()) {
                log_err("gentest_codegen: invalid module mock include insertion offset {} in '{}'\n", *mock_codegen_include_offset,
                        source_path.string());
                return std::nullopt;
            }
            rendered.append(original.data() + cursor, *mock_codegen_include_offset - cursor);
            rendered.append(render_module_mock_codegen_include_block());
            rendered.push_back('\n');
            cursor = *mock_codegen_include_offset;
            mock_codegen_include_offset.reset();
        }
        if (offset < cursor || offset > original.size()) {
            log_err("gentest_codegen: invalid module attachment insertion order in '{}'\n", source_path.string());
            return std::nullopt;
        }
        rendered.append(original.data() + cursor, offset - cursor);
        std::ranges::sort(group, {}, [](const MockClassInfo *mock) { return mock->qualified_name; });
        const auto &namespace_chain = group.front()->attachment_namespace_chain;
        for (const auto *mock : group) {
            if (!namespace_chains_equal(namespace_chain, mock->attachment_namespace_chain)) {
                log_err("gentest_codegen: inconsistent module attachment namespace scope at offset {} in '{}'\n", offset,
                        source_path.string());
                return std::nullopt;
            }
        }
        if (!namespace_chain.empty()) {
            rendered.append(render_namespace_scope_close(namespace_chain));
        }
        rendered.push_back('\n');
        for (const auto *mock : group) {
            rendered.append(render::render_module_mock_attachment(*mock));
            rendered.push_back('\n');
        }
        if (!namespace_chain.empty()) {
            rendered.append(render_namespace_scope_reopen(namespace_chain));
        }
        cursor = offset;
    }

    if (mock_codegen_include_offset.has_value()) {
        if (*mock_codegen_include_offset < cursor || *mock_codegen_include_offset > original.size()) {
            log_err("gentest_codegen: invalid module mock include insertion offset {} in '{}'\n", *mock_codegen_include_offset,
                    source_path.string());
            return std::nullopt;
        }
        rendered.append(original.data() + cursor, *mock_codegen_include_offset - cursor);
        rendered.append(render_module_mock_codegen_include_block());
        rendered.push_back('\n');
        cursor = *mock_codegen_include_offset;
    }

    rendered.append(original.data() + cursor, original.size() - cursor);
    return rendered;
}

auto make_unique_tmp_path(const fs::path &path) -> fs::path {
    auto current_process_id = []() -> std::uint32_t {
#if defined(_WIN32)
        return static_cast<std::uint32_t>(::_getpid());
#else
        return static_cast<std::uint32_t>(::getpid());
#endif
    };
    static std::atomic<std::uint32_t> seq{0};
    const std::uint32_t               nonce = seq.fetch_add(1u, std::memory_order_relaxed);
    const std::uint32_t               pid   = current_process_id();
    fs::path tmp_path = path;
    // Keep temp suffix short to avoid MAX_PATH failures on Windows for long output paths.
    tmp_path += fmt::format(".tmp.{:06x}.{:06x}", pid & 0xFFFFFFu, nonce & 0xFFFFFFu);
    return tmp_path;
}

auto make_short_unique_tmp_path_near(const fs::path &path) -> fs::path {
    auto current_process_id = []() -> std::uint32_t {
#if defined(_WIN32)
        return static_cast<std::uint32_t>(::_getpid());
#else
        return static_cast<std::uint32_t>(::getpid());
#endif
    };
    static std::atomic<std::uint32_t> seq{0};
    const std::uint32_t               nonce = seq.fetch_add(1u, std::memory_order_relaxed);
    const std::uint32_t               pid   = current_process_id();
    fs::path                          tmp_path;
    if (path.has_parent_path()) {
        tmp_path = path.parent_path();
    }
    tmp_path /= fmt::format(".gtmp.{:06x}.{:06x}", pid & 0xFFFFFFu, nonce & 0xFFFFFFu);
    return tmp_path;
}

bool read_file(const fs::path &path, std::string &out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !in.bad();
}

bool write_file_atomic_if_changed(const fs::path &path, std::string_view content) {
    std::string existing;
    if (read_file(path, existing) && existing == content) {
        return true;
    }

    auto write_file_direct = [&](const fs::path &target_path) -> bool {
        std::ofstream out(target_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            log_err("gentest_codegen: failed to open output file '{}'\n", target_path.string());
            return false;
        }
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.close();
        if (!out) {
            log_err("gentest_codegen: failed to write output file '{}'\n", target_path.string());
            return false;
        }
        return true;
    };

    auto try_write_tmp = [&](const fs::path &tmp_path) -> bool {
        std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.close();
        return static_cast<bool>(out);
    };

    fs::path tmp_path = make_unique_tmp_path(path);
    if (!try_write_tmp(tmp_path)) {
        // Some Windows paths can open the final file but fail once full filename + temp suffix is appended.
        tmp_path = make_short_unique_tmp_path_near(path);
        if (!try_write_tmp(tmp_path)) {
            return write_file_direct(path);
        }
    }

    std::error_code ec;
    fs::rename(tmp_path, path, ec);
    if (ec) {
        std::error_code remove_ec;
        fs::remove(path, remove_ec);
        ec.clear();
        fs::rename(tmp_path, path, ec);
        if (ec) {
            log_err("gentest_codegen: failed to replace output file '{}': {}\n", path.string(), ec.message());
            std::error_code cleanup_ec;
            fs::remove(tmp_path, cleanup_ec);
            return false;
        }
    }

    return true;
}

bool ensure_parent_dir(const fs::path &path) {
    if (!path.has_parent_path()) {
        return true;
    }
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
        log_err("gentest_codegen: failed to create directory '{}': {}\n", path.parent_path().string(), ec.message());
        return false;
    }
    return true;
}

} // namespace

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void replace_all(std::string &inout, std::string_view needle, std::string_view replacement) {
    const std::string target{needle};
    const std::string substitute{replacement};
    std::size_t       pos = 0;
    while ((pos = inout.find(target, pos)) != std::string::npos) {
        inout.replace(pos, target.size(), substitute);
        pos += substitute.size();
    }
}

auto render_cases(const CollectorOptions &options, const std::vector<TestCaseInfo> &cases,
                  const std::vector<FixtureDeclInfo> &fixtures) -> std::optional<std::string> {
    std::string template_content;
    if (!options.template_path.empty()) {
        template_content = render::read_template_file(options.template_path);
        if (template_content.empty()) {
            log_err("gentest_codegen: failed to load template file '{}', using built-in template.\n", options.template_path.string());
        }
    }
    if (template_content.empty())
        template_content = std::string(tpl::test_impl);

    // Load partials
    const auto tpl_wrapper_free      = std::string(tpl::wrapper_free);
    const auto tpl_wrapper_free_fix  = std::string(tpl::wrapper_free_fixtures);
    const auto tpl_wrapper_ephemeral = std::string(tpl::wrapper_ephemeral);
    const auto tpl_wrapper_stateful  = std::string(tpl::wrapper_stateful);
    const auto tpl_case_entry        = std::string(tpl::case_entry);
    const auto tpl_array_empty       = std::string(tpl::array_decl_empty);
    const auto tpl_array_nonempty    = std::string(tpl::array_decl_nonempty);
    const auto tpl_fwd_line          = std::string(tpl::forward_decl_line);
    const auto tpl_fwd_ns            = std::string(tpl::forward_decl_ns);

    std::string forward_decl_block = render::render_forward_decls(cases, tpl_fwd_line, tpl_fwd_ns);

    auto                     traits                  = render::render_trait_arrays(cases, tpl_array_empty, tpl_array_nonempty);
    std::string              trait_declarations      = std::move(traits.declarations);
    std::vector<std::string> tag_array_names         = std::move(traits.tag_names);
    std::vector<std::string> requirement_array_names = std::move(traits.req_names);

    const render::WrapperTemplates wrapper_templates{
        .free = tpl_wrapper_free,
        .free_fixtures = tpl_wrapper_free_fix,
        .ephemeral = tpl_wrapper_ephemeral,
        .stateful = tpl_wrapper_stateful,
    };
    std::string wrapper_impls = render::render_wrappers(cases, wrapper_templates);

    std::string case_entries;
    if (cases.empty()) {
        case_entries = "    // No test cases discovered during code generation.\n";
    } else {
        case_entries = render::render_case_entries(cases, tag_array_names, requirement_array_names, tpl_case_entry);
    }

    std::string fixture_registrations = render::render_fixture_registrations(fixtures);

    std::string output = template_content;
    replace_all(output, "{{REGISTRATION_COMMON}}", tpl::registration_common);
    replace_all(output, "{{FORWARD_DECLS}}", forward_decl_block);
    replace_all(output, "{{CASE_COUNT}}", std::to_string(cases.size()));
    replace_all(output, "{{TRAIT_DECLS}}", trait_declarations);
    replace_all(output, "{{WRAPPER_IMPLS}}", wrapper_impls);
    replace_all(output, "{{CASE_INITS}}", case_entries);
    replace_all(output, "{{FIXTURE_REGISTRATIONS}}", fixture_registrations);
    replace_all(output, "{{ENTRY_FUNCTION}}", options.entry);
    // Version for --help
#if defined(GENTEST_VERSION_STR)
    replace_all(output, "{{VERSION}}", GENTEST_VERSION_STR);
#else
    replace_all(output, "{{VERSION}}", "0.0.0");
#endif

    // Include sources in the generated file so fixture types are visible
    std::string    includes;
    includes.reserve(options.sources.size() * 32);
    const bool     skip_includes = !options.include_sources;
    const fs::path out_dir = options.output_path.has_parent_path() ? options.output_path.parent_path() : fs::current_path();
    for (const auto &src : options.sources) {
        if (skip_includes) break;
        fs::path spath(src);
        // Avoid `std::filesystem::proximate()` because it canonicalizes paths, which
        // can resolve symlink forests (e.g. Bazel execroot) into host paths that
        // don't exist in sandboxed builds.
        fs::path rel = spath.lexically_relative(out_dir);
        if (rel.empty()) {
            rel = spath;
        }
        std::string inc = rel.generic_string();
        fmt::format_to(std::back_inserter(includes), "#include \"{}\"\n", render::escape_string(inc));
    }
    replace_all(output, "{{INCLUDE_SOURCES}}", includes);

    // Mock registry and inline implementations are generated alongside the
    // test wrappers. Test sources that use mocking should include
    // `gentest/mock.h` after the mocked types are declared/defined.

    return output;
}

int emit(const CollectorOptions &opts, const std::vector<TestCaseInfo> &cases,
         const std::vector<FixtureDeclInfo> &fixtures, const std::vector<MockClassInfo> &mocks) {
    std::vector<TestCaseInfo> cases_for_render = cases;
    if (opts.source_root && !opts.source_root->empty()) {
        for (auto &c : cases_for_render) {
            c.filename = normalize_case_file(opts, c.filename);
        }
    }
    std::vector<FixtureDeclInfo> fixtures_for_render = fixtures;
    if (opts.source_root && !opts.source_root->empty()) {
        for (auto &f : fixtures_for_render) {
            f.filename = normalize_case_file(opts, f.filename);
        }
    }

    if (opts.tu_output_dir.empty()) {
        fs::path out_path = opts.output_path;
        if (!ensure_parent_dir(out_path)) {
            return 1;
        }

        // Embedded template is used when no template path is provided.

        const auto content = render_cases(opts, cases_for_render, fixtures_for_render);
        if (!content) {
            return 1;
        }

        if (!write_file_atomic_if_changed(out_path, *content)) {
            return 1;
        }
    } else {
        if (!ensure_dir(opts.tu_output_dir)) {
            return 1;
        }

        // Group discovered cases by their originating translation unit so we
        // can emit one wrapper TU per input source.
        std::map<std::string, std::vector<TestCaseInfo>> cases_by_tu;
        for (const auto &c : cases_for_render) {
            cases_by_tu[normalize_path_key(fs::path(c.tu_filename))].push_back(c);
        }
        std::map<std::string, std::vector<FixtureDeclInfo>> fixtures_by_tu;
        for (const auto &f : fixtures_for_render) {
            fixtures_by_tu[normalize_path_key(fs::path(f.tu_filename))].push_back(f);
        }
        std::unordered_map<std::string, std::vector<const MockClassInfo *>> direct_module_mocks_by_source;
        std::unordered_map<std::string, bool>                               module_mock_codegen_include_by_source;
        for (const auto &mock : mocks) {
            if (mock.definition_kind == MockClassInfo::DefinitionKind::NamedModule && !mock.definition_file.empty()) {
                direct_module_mocks_by_source[normalize_path_key(fs::path(mock.definition_file))].push_back(&mock);
            }
            if (mock.definition_kind != MockClassInfo::DefinitionKind::HeaderLike) {
                continue;
            }
            for (const auto &use_file : mock.use_files) {
                if (!use_file.empty()) {
                    module_mock_codegen_include_by_source[normalize_path_key(fs::path(use_file))] = true;
                }
            }
        }

        const auto tpl_wrapper_free      = std::string(tpl::wrapper_free);
        const auto tpl_wrapper_free_fix  = std::string(tpl::wrapper_free_fixtures);
        const auto tpl_wrapper_ephemeral = std::string(tpl::wrapper_ephemeral);
        const auto tpl_wrapper_stateful  = std::string(tpl::wrapper_stateful);
        const auto tpl_case_entry        = std::string(tpl::case_entry);
        const auto tpl_array_empty       = std::string(tpl::array_decl_empty);
        const auto tpl_array_nonempty    = std::string(tpl::array_decl_nonempty);
        const auto tpl_fwd_line          = std::string(tpl::forward_decl_line);
        const auto tpl_fwd_ns            = std::string(tpl::forward_decl_ns);

        const render::WrapperTemplates wrapper_templates{
            .free = tpl_wrapper_free,
            .free_fixtures = tpl_wrapper_free_fix,
            .ephemeral = tpl_wrapper_ephemeral,
            .stateful = tpl_wrapper_stateful,
        };

        // Guard against multiple input sources mapping to the same output header
        // name (would be nondeterministic under parallel emission).
        std::unordered_map<std::string, std::string> header_owner;
        header_owner.reserve(opts.sources.size());
        for (std::size_t idx = 0; idx < opts.sources.size(); ++idx) {
            const auto &src = opts.sources[idx];
            fs::path    header_out = resolve_tu_header_output(opts, idx);
            const std::string key = casefolded_path_key(header_out);
            auto              [it, inserted] = header_owner.emplace(key, src);
            if (!inserted) {
                log_err("gentest_codegen: multiple sources map to the same TU output header '{}': '{}' and '{}'\n", key, it->second, src);
                return 1;
            }
        }

        const std::size_t jobs = resolve_concurrency(opts.sources.size(), opts.jobs);
        std::vector<int>  statuses(opts.sources.size(), 0);
        parallel_for(opts.sources.size(), jobs, [&](std::size_t idx) {
            const fs::path source_path = fs::path(opts.sources[idx]);
            const std::string key      = normalize_path_key(source_path);
            auto it                    = cases_by_tu.find(key);
            std::vector<TestCaseInfo> tu_cases;
            if (it != cases_by_tu.end()) {
                tu_cases = it->second;
            }
            std::vector<FixtureDeclInfo> tu_fixtures;
            auto fit = fixtures_by_tu.find(key);
            if (fit != fixtures_by_tu.end()) {
                tu_fixtures = fit->second;
            }

            std::ranges::sort(tu_cases, {}, &TestCaseInfo::display_name);

            fs::path header_out = resolve_tu_header_output(opts, idx);
            if (!ensure_parent_dir(header_out)) {
                statuses[idx] = 1;
                return;
            }

            const auto parsed_idx = parse_tu_index(source_path.filename().string());
            const std::string register_fn =
                fmt::format("register_tu_{:04d}", parsed_idx.has_value() ? *parsed_idx : static_cast<std::uint32_t>(idx));

            // Registration header (compiled via a CMake-generated shim TU).
            std::string header_content = std::string(tpl::tu_registration_header);

            // Render test wrappers and cases for this TU.
            std::string forward_decl_block = render::render_forward_decls(tu_cases, tpl_fwd_line, tpl_fwd_ns);

            auto                     traits = render::render_trait_arrays(tu_cases, tpl_array_empty, tpl_array_nonempty);
            std::string              trait_declarations = std::move(traits.declarations);
            std::vector<std::string> tag_array_names = std::move(traits.tag_names);
            std::vector<std::string> requirement_array_names = std::move(traits.req_names);

            std::string wrapper_impls = render::render_wrappers(tu_cases, wrapper_templates);

            std::string case_entries;
            if (tu_cases.empty()) {
                case_entries = "    // No test cases discovered during code generation.\n";
            } else {
                case_entries = render::render_case_entries(tu_cases, tag_array_names, requirement_array_names, tpl_case_entry);
            }
            std::string fixture_registrations = render::render_fixture_registrations(tu_fixtures);

            replace_all(header_content, "{{REGISTRATION_COMMON}}", tpl::registration_common);
            replace_all(header_content, "{{FORWARD_DECLS}}", forward_decl_block);
            replace_all(header_content, "{{CASE_COUNT}}", std::to_string(tu_cases.size()));
            replace_all(header_content, "{{TRAIT_DECLS}}", trait_declarations);
            replace_all(header_content, "{{WRAPPER_IMPLS}}", wrapper_impls);
            replace_all(header_content, "{{CASE_INITS}}", case_entries);
            replace_all(header_content, "{{FIXTURE_REGISTRATIONS}}", fixture_registrations);
            replace_all(header_content, "{{REGISTER_FN}}", register_fn);

            if (!write_file_atomic_if_changed(header_out, header_content)) {
                statuses[idx] = 1;
                return;
            }

            if (is_module_interface_source(source_path)) {
                const auto mock_it = direct_module_mocks_by_source.find(key);
                const std::vector<const MockClassInfo *> empty_mocks;
                const auto &source_mocks = mock_it != direct_module_mocks_by_source.end() ? mock_it->second : empty_mocks;
                const bool needs_mock_codegen_include = module_mock_codegen_include_by_source.contains(key);
                const auto wrapper_source = render_module_wrapper_source(source_path, source_mocks, needs_mock_codegen_include);
                if (!wrapper_source.has_value()) {
                    statuses[idx] = 1;
                    return;
                }
                const fs::path module_wrapper_out = resolve_module_wrapper_output(opts, idx);
                if (!ensure_parent_dir(module_wrapper_out)) {
                    statuses[idx] = 1;
                    return;
                }
                if (!write_file_atomic_if_changed(module_wrapper_out, *wrapper_source)) {
                    statuses[idx] = 1;
                }
            }
        });

        if (std::ranges::any_of(statuses, [](int st) { return st != 0; })) {
            return 1;
        }
    }

    const bool have_mock_paths = !opts.mock_registry_path.empty() && !opts.mock_impl_path.empty();
    if (!mocks.empty() || have_mock_paths) {
        if (!have_mock_paths) {
            log_err_raw("gentest_codegen: mock outputs requested but --mock-registry/--mock-impl paths were not provided\n");
            return 1;
        }
        const auto          rendered = render::render_mocks(opts, mocks);
        if (!rendered.error.empty()) {
            log_err("gentest_codegen: {}\n", rendered.error);
            return 1;
        }
        render::MockOutputs outputs;
        if (rendered.outputs.has_value()) {
            outputs = std::move(*rendered.outputs);
        } else {
            outputs.registry_header     = "#pragma once\n\n// gentest_codegen: no mocks discovered.\n";
            outputs.implementation_unit = "// gentest_codegen: no mocks discovered.\n";
        }

        if (!ensure_parent_dir(opts.mock_registry_path) || !ensure_parent_dir(opts.mock_impl_path)) {
            return 1;
        }

        if (!write_file_atomic_if_changed(opts.mock_registry_path, outputs.registry_header)) {
            return 1;
        }
        if (!write_file_atomic_if_changed(opts.mock_impl_path, outputs.implementation_unit)) {
            return 1;
        }
        for (const auto &file : outputs.additional_files) {
            if (!ensure_parent_dir(file.path)) {
                return 1;
            }
            if (!write_file_atomic_if_changed(file.path, file.content)) {
                return 1;
            }
        }
    }

    return 0;
}

} // namespace gentest::codegen
