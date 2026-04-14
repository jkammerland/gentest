// Implementation of template-based emission for test cases

#include "emit.hpp"

#include "log.hpp"
#include "parallel_for.hpp"
#include "render.hpp"
#include "render_mocks.hpp"
#include "scan_utils.hpp"
#include "templates.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <iterator>
#include <llvm/ADT/StringRef.h>
#include <map>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace gentest::codegen {

namespace {
using gentest::codegen::scan::is_any_import_scan_line;
using gentest::codegen::scan::is_global_module_fragment_scan_line;
using gentest::codegen::scan::is_preprocessor_directive_scan_line;
using gentest::codegen::scan::looks_like_import_scan_prefix;
using gentest::codegen::scan::looks_like_named_module_scan_prefix;
using gentest::codegen::scan::named_module_name_from_source_file;
using gentest::codegen::scan::normalize_scan_module_preamble_source;
using gentest::codegen::scan::parse_imported_module_name_from_scan_line;
using gentest::codegen::scan::parse_include_header_from_scan_line;
using gentest::codegen::scan::parse_named_module_name_from_scan_line;
using gentest::codegen::scan::process_scan_physical_line;
using gentest::codegen::scan::ScanStreamState;
using gentest::codegen::scan::split_scan_statements;
using gentest::codegen::scan::strip_comments_for_line_scan;
using gentest::codegen::scan::trim_ascii_copy;

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
    if (!ec)
        abs = canon;
    abs             = abs.lexically_normal();
    std::string key = abs.generic_string();
#if defined(_WIN32)
    for (auto &ch : key)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
#endif
    return key;
}

std::string casefolded_path_key(const fs::path &path) {
    std::string key = normalize_path_key(path);
    for (auto &ch : key)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return key;
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
    if (auto canon = fs::weakly_canonical(abs_file, ec); !ec)
        abs_file = canon;
    ec.clear();
    if (auto canon = fs::weakly_canonical(abs_root, ec); !ec)
        abs_root = canon;

    abs_file = abs_file.lexically_normal();
    abs_root = abs_root.lexically_normal();

    auto to_lower = [](std::string value) {
#if defined(_WIN32)
        for (auto &ch : value)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
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
    std::size_t   i      = 3;
    std::uint32_t value  = 0;
    std::size_t   digits = 0;
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

bool is_module_interface_source(const CollectorOptions &opts, const fs::path &path) {
    if (opts.module_interface_sources.contains(path.string())) {
        return true;
    }
    return named_module_name_from_source_file(path).has_value();
}

fs::path resolve_module_wrapper_output(const CollectorOptions &opts, std::size_t idx) { return opts.module_wrapper_outputs[idx]; }

bool read_file(const fs::path &path, std::string &out);

struct SourceMockCodegenIncludes {
    struct Range {
        std::size_t begin = 0;
        std::size_t end   = 0;
    };

    bool               has_mock_codegen     = false;
    bool               has_registry_codegen = false;
    bool               has_impl_codegen     = false;
    bool               has_mock_api_header  = false;
    std::vector<Range> manual_codegen_include_ranges;

    [[nodiscard]] bool has_complete_manual_codegen() const { return has_mock_codegen || (has_registry_codegen && has_impl_codegen); }
    [[nodiscard]] bool has_any_manual_codegen() const { return has_mock_codegen || has_registry_codegen || has_impl_codegen; }
};

struct PerSourceEmitData {
    std::vector<TestCaseInfo>          cases;
    std::vector<FixtureDeclInfo>       fixtures;
    std::vector<const MockClassInfo *> direct_module_mocks;
    bool                               needs_mock_codegen_include = false;
};

bool is_mock_codegen_header_name(std::string_view header) {
    return header == "gentest/mock_codegen.h" || header == "gentest/mock_registry_codegen.h" || header == "gentest/mock_impl_codegen.h";
}

SourceMockCodegenIncludes scan_source_mock_codegen_includes(std::string_view text) {
    SourceMockCodegenIncludes includes;
    std::size_t               cursor = 0;
    ScanStreamState           scan_state;
    scan_state.warn_on_unknown_conditions = false;

    while (cursor < text.size()) {
        const std::size_t line_end = text.find('\n', cursor);
        const std::size_t next     = line_end == std::string_view::npos ? text.size() : line_end + 1;
        std::string_view  line     = text.substr(cursor, next - cursor);
        if (!line.empty() && line.back() == '\n') {
            line.remove_suffix(1);
        }
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }

        const bool branch_active_before = scan_state.current_branch_active;
        const auto processed            = process_scan_physical_line(line, scan_state);

        auto record_header = [&](const std::optional<std::string> &header) {
            if (!header.has_value()) {
                return;
            }
            includes.has_mock_codegen     = includes.has_mock_codegen || *header == "gentest/mock_codegen.h";
            includes.has_registry_codegen = includes.has_registry_codegen || *header == "gentest/mock_registry_codegen.h";
            includes.has_impl_codegen     = includes.has_impl_codegen || *header == "gentest/mock_impl_codegen.h";
            includes.has_mock_api_header  = includes.has_mock_api_header || *header == "gentest/mock.h";
            if (is_mock_codegen_header_name(*header)) {
                includes.manual_codegen_include_ranges.push_back(SourceMockCodegenIncludes::Range{
                    .begin = cursor,
                    .end   = next,
                });
            }
        };

        if (processed.is_active_code) {
            record_header(parse_include_header_from_scan_line(processed.stripped));
        } else if (processed.is_preprocessor && branch_active_before) {
            record_header(parse_include_header_from_scan_line(line));
        }
        cursor = next;
    }

    return includes;
}

struct ModuleGlobalFragmentInsertLocation {
    std::size_t offset                     = 0;
    bool        synthesize_global_fragment = false;
};

std::optional<ModuleGlobalFragmentInsertLocation> find_module_global_fragment_insert_location(std::string_view text) {
    bool            seen_global_fragment  = false;
    std::size_t     cursor                = 0;
    std::size_t     global_fragment_after = 0;
    std::string     pending_module;
    ScanStreamState scan_state;
    scan_state.warn_on_unknown_conditions = false;

    while (cursor < text.size()) {
        const std::size_t line_end = text.find('\n', cursor);
        const std::size_t next     = line_end == std::string_view::npos ? text.size() : line_end + 1;
        std::string_view  line     = text.substr(cursor, next - cursor);
        if (!line.empty() && line.back() == '\n') {
            line.remove_suffix(1);
        }
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        const auto processed = process_scan_physical_line(line, scan_state);
        if (!processed.is_active_code) {
            cursor = next;
            continue;
        }

        for (const auto &statement : split_scan_statements(processed.stripped)) {
            if (statement.empty()) {
                continue;
            }

            if (is_global_module_fragment_scan_line(statement)) {
                seen_global_fragment  = true;
                global_fragment_after = next;
                pending_module.clear();
                continue;
            }

            if (pending_module.empty()) {
                if (!looks_like_named_module_scan_prefix(statement)) {
                    continue;
                }
                pending_module = statement;
            } else {
                pending_module.push_back(' ');
                pending_module.append(statement);
            }

            if (statement.find(';') == std::string::npos) {
                continue;
            }

            if (parse_named_module_name_from_scan_line(pending_module).has_value()) {
                if (seen_global_fragment) {
                    return ModuleGlobalFragmentInsertLocation{
                        .offset                     = global_fragment_after,
                        .synthesize_global_fragment = false,
                    };
                }
                return ModuleGlobalFragmentInsertLocation{
                    .offset                     = 0,
                    .synthesize_global_fragment = true,
                };
            }
            pending_module.clear();
        }
        cursor = next;
    }

    return std::nullopt;
}

std::optional<std::size_t> find_module_purview_import_insert_offset(std::string_view text) {
    std::size_t     cursor = 0;
    std::string     pending_module;
    ScanStreamState scan_state;
    scan_state.warn_on_unknown_conditions = false;

    while (cursor < text.size()) {
        const std::size_t line_end = text.find('\n', cursor);
        const std::size_t next     = line_end == std::string_view::npos ? text.size() : line_end + 1;
        std::string_view  line     = text.substr(cursor, next - cursor);
        if (!line.empty() && line.back() == '\n') {
            line.remove_suffix(1);
        }
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        const auto processed = process_scan_physical_line(line, scan_state);
        if (!processed.is_active_code) {
            cursor = next;
            continue;
        }

        for (const auto &statement : split_scan_statements(processed.stripped)) {
            if (statement.empty()) {
                continue;
            }

            if (is_global_module_fragment_scan_line(statement)) {
                pending_module.clear();
                continue;
            }

            if (pending_module.empty()) {
                if (!looks_like_named_module_scan_prefix(statement)) {
                    continue;
                }
                pending_module = statement;
            } else {
                pending_module.push_back(' ');
                pending_module.append(statement);
            }

            if (statement.find(';') == std::string::npos) {
                continue;
            }

            if (parse_named_module_name_from_scan_line(pending_module).has_value()) {
                return next;
            }
            pending_module.clear();
        }
        cursor = next;
    }

    return std::nullopt;
}

void append_original_segment_skipping_ranges(std::string &rendered, std::string_view original, std::size_t &cursor, std::size_t target,
                                             const std::vector<SourceMockCodegenIncludes::Range> &skipped_ranges,
                                             std::size_t                                         &skipped_idx) {
    while (cursor < target) {
        while (skipped_idx < skipped_ranges.size() && skipped_ranges[skipped_idx].end <= cursor) {
            ++skipped_idx;
        }
        if (skipped_idx < skipped_ranges.size() && skipped_ranges[skipped_idx].begin <= cursor) {
            cursor = std::min(target, skipped_ranges[skipped_idx].end);
            if (cursor >= skipped_ranges[skipped_idx].end) {
                ++skipped_idx;
            }
            continue;
        }

        std::size_t next = target;
        if (skipped_idx < skipped_ranges.size()) {
            next = std::min(next, skipped_ranges[skipped_idx].begin);
        }
        if (next > cursor) {
            rendered.append(original.data() + cursor, next - cursor);
            cursor = next;
        }
    }
}

std::string render_module_mock_api_import_block() {
    std::string out;
    out.reserve(80);
    out.append("\n// gentest_codegen: injected mock API import.\n");
    out.append("import gentest.mock;\n");
    return out;
}

std::string render_module_mock_api_include_block() {
    std::string out;
    out.reserve(192);
    out.append("\n// gentest_codegen: injected mock API include.\n");
    out.append("#define GENTEST_NO_AUTO_MOCK_INCLUDE 1\n");
    out.append("#include \"gentest/mock.h\"\n");
    out.append("#undef GENTEST_NO_AUTO_MOCK_INCLUDE\n");
    return out;
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

std::string render_module_registration_support_include_block() {
    std::string out;
    out.reserve(320);
    out.append("\n// gentest_codegen: injected registration support includes.\n");
    out.append("#include <array>\n");
    out.append("#include <fmt/format.h>\n");
    out.append("#include <memory>\n");
    out.append("#include <span>\n");
    out.append("#include <string>\n");
    out.append("#include <string_view>\n");
    out.append("#include <type_traits>\n");
    out.append("#include <utility>\n");
    out.append("#include \"gentest/detail/fixture_runtime.h\"\n");
    out.append("#include \"gentest/detail/registry_runtime.h\"\n");
    out.append("#include \"gentest/runner.h\"\n");
    return out;
}

std::string render_module_registration_include_block(std::string_view registration_header_name) {
    std::string out;
    out.reserve(registration_header_name.size() + 192);
    out.append("\n// gentest_codegen: injected TU registration include.\n");
    out.append("#if !defined(GENTEST_CODEGEN) && __has_include(\"");
    out.append(registration_header_name);
    out.append("\")\n");
    out.append("#define GENTEST_TU_REGISTRATION_HEADER_NO_PREAMBLE 1\n");
    out.append("#include \"");
    out.append(registration_header_name);
    out.append("\"\n");
    out.append("#undef GENTEST_TU_REGISTRATION_HEADER_NO_PREAMBLE\n");
    out.append("#endif\n");
    return out;
}

bool namespace_chains_equal(const std::vector<MockNamespaceScopeInfo> &lhs, const std::vector<MockNamespaceScopeInfo> &rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        if (lhs[i].name != rhs[i].name || lhs[i].is_inline != rhs[i].is_inline || lhs[i].is_exported != rhs[i].is_exported ||
            lhs[i].lexical_close_group != rhs[i].lexical_close_group || lhs[i].reopen_prefix != rhs[i].reopen_prefix) {
            return false;
        }
    }
    return true;
}

std::string render_namespace_scope_close(const std::vector<MockNamespaceScopeInfo> &chain) {
    std::string out;
    out.reserve(chain.size() * 32);
    std::size_t last_group = 0;
    bool        have_group = false;
    for (const auto &it : std::ranges::reverse_view(chain)) {
        if (have_group && it.lexical_close_group == last_group) {
            continue;
        }
        fmt::format_to(std::back_inserter(out), "\n}} // namespace {}\n", it.name);
        last_group = it.lexical_close_group;
        have_group = true;
    }
    return out;
}

std::string render_namespace_scope_reopen(const std::vector<MockNamespaceScopeInfo> &chain) {
    std::string out;
    out.reserve(chain.size() * 40);
    for (const auto &ns : chain) {
        if (ns.reopen_prefix.empty()) {
            continue;
        }
        if (ns.is_exported) {
            out += "export ";
        }
        fmt::format_to(std::back_inserter(out), "{} {{\n", ns.reopen_prefix);
    }
    return out;
}

std::optional<std::string> render_module_wrapper_source(const fs::path &source_path, const std::vector<const MockClassInfo *> &source_mocks,
                                                        bool needs_mock_codegen_include, std::string_view registration_header_name) {
    std::string raw_original;
    if (!read_file(source_path, raw_original)) {
        log_err("gentest_codegen: failed to read module source '{}'\n", source_path.string());
        return std::nullopt;
    }
    std::string original = normalize_scan_module_preamble_source(raw_original);

    const SourceMockCodegenIncludes manual_includes                   = scan_source_mock_codegen_includes(original);
    const bool                      has_manual_mock_codegen_includes  = manual_includes.has_any_manual_codegen();
    const bool                      needs_global_mock_codegen_include = needs_mock_codegen_include && !has_manual_mock_codegen_includes;
    const bool needs_mock_api_import = !source_mocks.empty() && !needs_global_mock_codegen_include && !has_manual_mock_codegen_includes;
    const bool needs_mock_api_include =
        (!source_mocks.empty() || needs_mock_codegen_include || has_manual_mock_codegen_includes) && !needs_mock_api_import;
    const bool needs_registration_header = !registration_header_name.empty();
    if (source_mocks.empty() && !needs_global_mock_codegen_include && !needs_mock_api_include && !needs_registration_header) {
        return original;
    }

    std::map<std::size_t, std::vector<const MockClassInfo *>> mocks_by_offset;
    std::size_t                                               reserve_extra = 0;
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
    std::optional<ModuleGlobalFragmentInsertLocation> global_include_location;
    if (needs_global_mock_codegen_include || needs_mock_api_include || needs_registration_header) {
        global_include_location = find_module_global_fragment_insert_location(original);
        if (!global_include_location.has_value()) {
            log_err("gentest_codegen: failed to locate module global-fragment insertion point in '{}'\n", source_path.string());
            return std::nullopt;
        }
        reserve_extra += 256;
        if (global_include_location->synthesize_global_fragment) {
            reserve_extra += 16;
        }
        if (needs_registration_header) {
            reserve_extra += registration_header_name.size() + 256;
        }
        if (needs_mock_api_include) {
            reserve_extra += 96;
        }
    }
    std::optional<std::size_t> mock_api_import_offset;
    if (needs_mock_api_import) {
        mock_api_import_offset = find_module_purview_import_insert_offset(original);
        if (!mock_api_import_offset.has_value()) {
            log_err("gentest_codegen: failed to locate module import insertion point in '{}'\n", source_path.string());
            return std::nullopt;
        }
        reserve_extra += 80;
    }

    if (mocks_by_offset.empty() && !global_include_location.has_value() && !mock_api_import_offset.has_value() &&
        !needs_registration_header) {
        return original;
    }

    std::string rendered;
    rendered.reserve(original.size() + reserve_extra);
    const std::vector<SourceMockCodegenIncludes::Range> empty_skipped_ranges;
    const auto                                         &skipped_manual_codegen_ranges =
        has_manual_mock_codegen_includes ? empty_skipped_ranges : manual_includes.manual_codegen_include_ranges;

    std::size_t       cursor        = 0;
    std::size_t       skipped_idx   = 0;
    const std::size_t import_offset = mock_api_import_offset.value_or(original.size() + 1);

    if (global_include_location.has_value()) {
        if (global_include_location->offset > original.size()) {
            log_err("gentest_codegen: invalid module global-fragment insertion offset {} in '{}'\n", global_include_location->offset,
                    source_path.string());
            return std::nullopt;
        }
        append_original_segment_skipping_ranges(rendered, original, cursor, global_include_location->offset, skipped_manual_codegen_ranges,
                                                skipped_idx);
        if (global_include_location->synthesize_global_fragment) {
            rendered.append("module;\n");
        }
        if (needs_registration_header) {
            rendered.append(render_module_registration_support_include_block());
            rendered.push_back('\n');
        }
        if (needs_mock_api_include) {
            rendered.append(render_module_mock_api_include_block());
            rendered.push_back('\n');
        }
        if (needs_global_mock_codegen_include) {
            rendered.append(render_module_mock_codegen_include_block());
            rendered.push_back('\n');
        }
    }

    if (mock_api_import_offset.has_value()) {
        if (*mock_api_import_offset > original.size()) {
            log_err("gentest_codegen: invalid module import insertion offset {} in '{}'\n", *mock_api_import_offset, source_path.string());
            return std::nullopt;
        }
        if (global_include_location.has_value() && global_include_location->offset > *mock_api_import_offset) {
            log_err("gentest_codegen: module import insertion point precedes global include insertion in '{}'\n", source_path.string());
            return std::nullopt;
        }
        append_original_segment_skipping_ranges(rendered, original, cursor, *mock_api_import_offset, skipped_manual_codegen_ranges,
                                                skipped_idx);
        rendered.append(render_module_mock_api_import_block());
        rendered.push_back('\n');
    }

    for (auto &[offset, group] : mocks_by_offset) {
        if (mock_api_import_offset.has_value() && offset < import_offset) {
            log_err("gentest_codegen: module attachment insertion precedes mock API import in '{}'\n", source_path.string());
            return std::nullopt;
        }
        if (offset < cursor || offset > original.size()) {
            log_err("gentest_codegen: invalid module attachment insertion order in '{}'\n", source_path.string());
            return std::nullopt;
        }
        append_original_segment_skipping_ranges(rendered, original, cursor, offset, skipped_manual_codegen_ranges, skipped_idx);
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

    append_original_segment_skipping_ranges(rendered, original, cursor, original.size(), skipped_manual_codegen_ranges, skipped_idx);
    if (needs_registration_header) {
        rendered.append(render_module_registration_include_block(registration_header_name));
    }
    return normalize_scan_module_preamble_source(rendered);
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
    const std::uint32_t               nonce    = seq.fetch_add(1u, std::memory_order_relaxed);
    const std::uint32_t               pid      = current_process_id();
    fs::path                          tmp_path = path;
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

bool requires_global_wrapper_impls_placeholder(const std::string &template_content) {
    if (template_content.find("{{GLOBAL_WRAPPER_IMPLS}}") != std::string::npos) {
        return false;
    }
    return template_content.find("{{WRAPPER_IMPLS}}") != std::string::npos ||
           template_content.find("{{REGISTRATION_COMMON}}") != std::string::npos;
}

struct RegistrationRenderTemplates {
    std::string wrapper_free;
    std::string wrapper_free_fixtures;
    std::string wrapper_ephemeral;
    std::string wrapper_stateful;
    std::string case_entry;
    std::string array_decl_empty;
    std::string array_decl_nonempty;
    std::string forward_decl_line;
    std::string forward_decl_ns;

    [[nodiscard]] render::WrapperTemplates wrapper_templates() const {
        return render::WrapperTemplates{
            .free          = wrapper_free,
            .free_fixtures = wrapper_free_fixtures,
            .ephemeral     = wrapper_ephemeral,
            .stateful      = wrapper_stateful,
        };
    }
};

struct RenderedRegistrationCore {
    std::string forward_decls;
    std::string trait_decls;
    std::string wrapper_impls;
    std::string case_inits;
    std::string fixture_registrations;
    std::size_t case_count = 0;
};

[[nodiscard]] RegistrationRenderTemplates load_registration_render_templates() {
    return RegistrationRenderTemplates{
        .wrapper_free          = std::string(tpl::wrapper_free),
        .wrapper_free_fixtures = std::string(tpl::wrapper_free_fixtures),
        .wrapper_ephemeral     = std::string(tpl::wrapper_ephemeral),
        .wrapper_stateful      = std::string(tpl::wrapper_stateful),
        .case_entry            = std::string(tpl::case_entry),
        .array_decl_empty      = std::string(tpl::array_decl_empty),
        .array_decl_nonempty   = std::string(tpl::array_decl_nonempty),
        .forward_decl_line     = std::string(tpl::forward_decl_line),
        .forward_decl_ns       = std::string(tpl::forward_decl_ns),
    };
}

[[nodiscard]] RenderedRegistrationCore render_registration_core(const std::vector<TestCaseInfo>    &cases,
                                                                const std::vector<FixtureDeclInfo> &fixtures,
                                                                const RegistrationRenderTemplates  &templates) {
    RenderedRegistrationCore core;
    core.case_count    = cases.size();
    core.forward_decls = render::render_forward_decls(cases, templates.forward_decl_line, templates.forward_decl_ns);

    auto traits      = render::render_trait_arrays(cases, templates.array_decl_empty, templates.array_decl_nonempty);
    core.trait_decls = std::move(traits.declarations);
    std::vector<std::string> tag_array_names         = std::move(traits.tag_names);
    std::vector<std::string> requirement_array_names = std::move(traits.req_names);

    core.wrapper_impls = render::render_wrappers(cases, templates.wrapper_templates());
    if (cases.empty()) {
        core.case_inits = "    // No test cases discovered during code generation.\n";
    } else {
        core.case_inits = render::render_case_entries(cases, tag_array_names, requirement_array_names, templates.case_entry);
    }
    core.fixture_registrations = render::render_fixture_registrations(fixtures);
    return core;
}

void apply_registration_core(std::string &output, const RenderedRegistrationCore &core) {
    replace_all(output, "{{WRAPPER_SUPPORT_COMMON}}", tpl::wrapper_support_common);
    replace_all(output, "{{REGISTRATION_COMMON}}", tpl::registration_common);
    replace_all(output, "{{FORWARD_DECLS}}", core.forward_decls);
    replace_all(output, "{{CASE_COUNT}}", std::to_string(core.case_count));
    replace_all(output, "{{TRAIT_DECLS}}", core.trait_decls);
    if (output.find("{{GLOBAL_WRAPPER_IMPLS}}") != std::string::npos) {
        replace_all(output, "{{GLOBAL_WRAPPER_IMPLS}}", core.wrapper_impls);
        replace_all(output, "{{WRAPPER_IMPLS}}", "");
    } else {
        replace_all(output, "{{WRAPPER_IMPLS}}", core.wrapper_impls);
    }
    replace_all(output, "{{CASE_INITS}}", core.case_inits);
    replace_all(output, "{{FIXTURE_REGISTRATIONS}}", core.fixture_registrations);
}

auto render_cases(const CollectorOptions &options, const std::vector<TestCaseInfo> &cases, const std::vector<FixtureDeclInfo> &fixtures)
    -> std::optional<std::string> {
    std::string template_content;
    if (!options.template_path.empty()) {
        template_content = render::read_template_file(options.template_path);
        if (template_content.empty()) {
            log_err("gentest_codegen: failed to load template file '{}', using built-in template.\n", options.template_path.string());
        }
    }
    if (template_content.empty())
        template_content = std::string(tpl::test_impl);

    if (requires_global_wrapper_impls_placeholder(template_content)) {
        log_err(
            "gentest_codegen: template file '{}' must use {{GLOBAL_WRAPPER_IMPLS}}; legacy {{WRAPPER_IMPLS}} placement is unsupported\n",
            options.template_path.string());
        return std::nullopt;
    }

    const auto templates = load_registration_render_templates();
    const auto core      = render_registration_core(cases, fixtures, templates);

    std::string output = template_content;
    apply_registration_core(output, core);
    replace_all(output, "{{ENTRY_FUNCTION}}", options.entry);
    // Version for --help
#if defined(GENTEST_VERSION_STR)
    replace_all(output, "{{VERSION}}", GENTEST_VERSION_STR);
#else
    replace_all(output, "{{VERSION}}", "0.0.0");
#endif

    // Include sources in the generated file so fixture types are visible
    std::string includes;
    includes.reserve(options.sources.size() * 32);
    const bool     skip_includes = !options.include_sources;
    const fs::path out_dir       = options.output_path.has_parent_path() ? options.output_path.parent_path() : fs::current_path();
    for (const auto &src : options.sources) {
        if (skip_includes)
            break;
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

int emit(const CollectorOptions &opts, const std::vector<TestCaseInfo> &cases, const std::vector<FixtureDeclInfo> &fixtures,
         const std::vector<MockClassInfo> &mocks) {
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
        std::unordered_map<std::string, PerSourceEmitData> per_source;
        per_source.reserve(cases_for_render.size() + fixtures_for_render.size() + mocks.size());
        for (const auto &c : cases_for_render) {
            per_source[normalize_path_key(fs::path(c.tu_filename))].cases.push_back(c);
        }
        for (const auto &f : fixtures_for_render) {
            per_source[normalize_path_key(fs::path(f.tu_filename))].fixtures.push_back(f);
        }
        for (const auto &mock : mocks) {
            if (mock.definition_kind == MockClassInfo::DefinitionKind::NamedModule && !mock.definition_file.empty()) {
                per_source[normalize_path_key(fs::path(mock.definition_file))].direct_module_mocks.push_back(&mock);
            }
            if (mock.definition_kind != MockClassInfo::DefinitionKind::HeaderLike) {
                continue;
            }
            for (const auto &use_file : mock.use_files) {
                if (!use_file.empty()) {
                    per_source[normalize_path_key(fs::path(use_file))].needs_mock_codegen_include = true;
                }
            }
        }
        for (auto &[_, data] : per_source) {
            std::ranges::sort(data.cases, {}, &TestCaseInfo::display_name);
        }

        const auto templates = load_registration_render_templates();

        // Guard against multiple input sources mapping to the same output header
        // name (would be nondeterministic under parallel emission).
        std::unordered_map<std::string, std::string> header_owner;
        header_owner.reserve(opts.sources.size());
        for (std::size_t idx = 0; idx < opts.sources.size(); ++idx) {
            const auto       &src        = opts.sources[idx];
            fs::path          header_out = resolve_tu_header_output(opts, idx);
            const std::string key        = casefolded_path_key(header_out);
            auto [it, inserted]          = header_owner.emplace(key, src);
            if (!inserted) {
                log_err("gentest_codegen: multiple sources map to the same TU output header '{}': '{}' and '{}'\n", key, it->second, src);
                return 1;
            }
        }

        const std::size_t                        jobs = resolve_concurrency(opts.sources.size(), opts.jobs);
        std::vector<int>                         statuses(opts.sources.size(), 0);
        const std::vector<TestCaseInfo>          empty_cases;
        const std::vector<FixtureDeclInfo>       empty_fixtures;
        const std::vector<const MockClassInfo *> empty_mocks;
        parallel_for(opts.sources.size(), jobs, [&](std::size_t idx) {
            const fs::path           source_path = fs::path(opts.sources[idx]);
            const std::string        key         = normalize_path_key(source_path);
            const auto               source_it   = per_source.find(key);
            const PerSourceEmitData *source_data = source_it != per_source.end() ? &source_it->second : nullptr;
            const auto              &tu_cases    = source_data ? source_data->cases : empty_cases;
            const auto              &tu_fixtures = source_data ? source_data->fixtures : empty_fixtures;

            fs::path header_out = resolve_tu_header_output(opts, idx);
            if (!ensure_parent_dir(header_out)) {
                statuses[idx] = 1;
                return;
            }

            const auto        parsed_idx = parse_tu_index(source_path.filename().string());
            const std::string register_fn =
                fmt::format("register_tu_{:04d}", parsed_idx.has_value() ? *parsed_idx : static_cast<std::uint32_t>(idx));

            std::string header_content;
            if (tu_cases.empty() && tu_fixtures.empty()) {
                header_content = "// This file is auto-generated by gentest_codegen.\n"
                                 "// Do not edit manually.\n\n"
                                 "#pragma once\n\n"
                                 "// No gentest registrations were discovered for this translation unit.\n";
            } else {
                // Registration header (compiled via a CMake-generated shim TU).
                header_content  = std::string(tpl::tu_registration_header);
                const auto core = render_registration_core(tu_cases, tu_fixtures, templates);
                apply_registration_core(header_content, core);
                replace_all(header_content, "{{REGISTER_FN}}", register_fn);
            }

            if (!write_file_atomic_if_changed(header_out, header_content)) {
                statuses[idx] = 1;
                return;
            }

            if (is_module_interface_source(opts, source_path)) {
                const auto       &source_mocks               = source_data ? source_data->direct_module_mocks : empty_mocks;
                const bool        needs_mock_codegen_include = source_data && source_data->needs_mock_codegen_include;
                const std::string registration_header_name =
                    (!tu_cases.empty() || !tu_fixtures.empty()) ? header_out.filename().string() : "";
                const auto wrapper_source =
                    render_module_wrapper_source(source_path, source_mocks, needs_mock_codegen_include, registration_header_name);
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
        const auto rendered = render::render_mocks(opts, mocks);
        if (!rendered.error.empty()) {
            log_err("gentest_codegen: {}\n", rendered.error);
            return 1;
        }
        render::MockOutputs outputs;
        if (rendered.outputs.has_value()) {
            outputs = *rendered.outputs;
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
