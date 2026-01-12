// Implementation of template-based emission for test cases

#include "emit.hpp"

#include "render.hpp"
#include "render_mocks.hpp"
#include "templates.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fmt/core.h>
#include <fstream>
#include <iterator>
#include <llvm/Support/raw_ostream.h>
#include <map>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace gentest::codegen {

namespace {

namespace fs = std::filesystem;

fs::path normalize_path(const fs::path &path) {
    std::error_code ec;
    fs::path        out = path;
    if (!out.is_absolute()) {
        out = fs::absolute(out, ec);
        if (ec) {
            return path;
        }
    }
    ec.clear();
    out = fs::weakly_canonical(out, ec);
    if (ec) {
        return path;
    }
    return out;
}

bool is_path_within(const fs::path &path, const fs::path &root) {
    if (root.empty())
        return false;
    auto path_it = path.begin();
    for (auto root_it = root.begin(); root_it != root.end(); ++root_it, ++path_it) {
        if (path_it == path.end() || *path_it != *root_it) {
            return false;
        }
    }
    return true;
}

bool resolve_include_path(std::string_view include_path, const fs::path &src_dir, const std::vector<fs::path> &include_roots,
                          fs::path &resolved) {
    const fs::path include_rel{std::string(include_path)};
    std::error_code ec;
    if (!src_dir.empty()) {
        const fs::path candidate = src_dir / include_rel;
        if (fs::exists(candidate, ec) && !ec) {
            resolved = normalize_path(candidate);
            return true;
        }
    }
    for (const auto &root : include_roots) {
        if (root.empty())
            continue;
        const fs::path candidate = root / include_rel;
        if (fs::exists(candidate, ec) && !ec) {
            resolved = normalize_path(candidate);
            return true;
        }
    }
    return false;
}

bool rewrite_include_to_root(const fs::path &resolved, const std::vector<fs::path> &include_roots, std::string &rewritten) {
    const fs::path normalized = normalize_path(resolved);
    for (const auto &root : include_roots) {
        if (root.empty())
            continue;
        if (!is_path_within(normalized, root))
            continue;
        const fs::path rel = normalized.lexically_relative(root);
        if (rel.empty() || rel.is_absolute())
            continue;
        rewritten = rel.generic_string();
        return true;
    }
    return false;
}

std::string include_path_for_generated(const fs::path &out_dir, const fs::path &header_path) {
    const fs::path abs_hdr = normalize_path(header_path);
    fs::path       rel     = abs_hdr.lexically_relative(normalize_path(out_dir));
    if (rel.empty() || rel.is_absolute()) {
        rel = header_path.filename();
    }
    return rel.generic_string();
}

bool is_header_include_target(std::string_view path) {
    const fs::path          p{std::string(path)};
    const std::string       ext = p.extension().string();
    std::string             lower;
    lower.resize(ext.size());
    std::transform(ext.begin(), ext.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower.empty())
        return true;
    return !(lower == ".c" || lower == ".cc" || lower == ".cpp" || lower == ".cxx" || lower == ".c++" || lower == ".m" || lower == ".mm");
}

std::vector<std::string> collect_header_includes(const std::vector<std::string> &sources, const std::vector<fs::path> &include_roots) {
    std::vector<std::string> includes;
    std::set<std::string>    seen;
    std::vector<fs::path>    normalized_roots;
    normalized_roots.reserve(include_roots.size());
    for (const auto &root : include_roots) {
        if (!root.empty()) {
            normalized_roots.push_back(normalize_path(root));
        }
    }

    for (const auto &src : sources) {
        std::ifstream in(src);
        if (!in)
            continue;
        const fs::path src_path = normalize_path(src);
        const fs::path src_dir  = src_path.has_parent_path() ? src_path.parent_path() : fs::path{};
        std::string line;
        while (std::getline(in, line)) {
            std::string_view view{line};
            std::size_t      pos = view.find_first_not_of(" \t");
            if (pos == std::string_view::npos || view[pos] != '#')
                continue;
            pos = view.find_first_not_of(" \t", pos + 1);
            if (pos == std::string_view::npos)
                continue;
            if (view.compare(pos, 7, "include") != 0)
                continue;
            pos = view.find_first_not_of(" \t", pos + 7);
            if (pos == std::string_view::npos)
                continue;
            const char open = view[pos];
            if (open != '"' && open != '<')
                continue;
            const char close = (open == '"') ? '"' : '>';
            const std::size_t end = view.find(close, pos + 1);
            if (end == std::string_view::npos || end <= pos + 1)
                continue;
            const std::string path = std::string(view.substr(pos + 1, end - pos - 1));
            if (path == "gentest/mock.h")
                continue;
            if (!is_header_include_target(path))
                continue;
            std::string include_path = path;
            if (open == '"') {
                fs::path resolved;
                if (resolve_include_path(path, src_dir, normalized_roots, resolved)) {
                    std::string rewritten;
                    if (rewrite_include_to_root(resolved, normalized_roots, rewritten)) {
                        include_path = std::move(rewritten);
                    }
                }
            }
            std::string inc_line;
            inc_line.reserve(include_path.size() + 12);
            inc_line += "#include ";
            inc_line += open;
            inc_line += include_path;
            inc_line += close;
            if (seen.insert(inc_line).second)
                includes.push_back(std::move(inc_line));
        }
    }

    return includes;
}

std::string render_test_decls_header(const CollectorOptions &options, const std::vector<TestCaseInfo> &cases) {
    std::string out;
    out += "// This file is auto-generated by gentest_codegen.\n";
    out += "// Do not edit manually.\n\n";
    out += "#pragma once\n\n";

    for (const auto &line : collect_header_includes(options.sources, options.include_roots)) {
        out += line;
        out += '\n';
    }
    out += '\n';

    const auto tpl_fwd_line = std::string(tpl::forward_decl_line);
    const auto tpl_fwd_ns   = std::string(tpl::forward_decl_ns);
    const auto fwd          = render::render_forward_decls(cases, tpl_fwd_line, tpl_fwd_ns);
    out += fwd;
    if (!out.empty() && out.back() != '\n')
        out += '\n';
    return out;
}

auto make_unique_tmp_path(const fs::path &path) -> fs::path {
    const auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
                            .count();
    const auto rand_hi = static_cast<std::uint64_t>(std::random_device{}());
    const auto rand_lo = static_cast<std::uint64_t>(std::random_device{}());
    const auto nonce   = (rand_hi << 32) ^ rand_lo;

    fs::path tmp_path = path;
    tmp_path += fmt::format(".tmp.{}.{}", now_ns, nonce);
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

    const fs::path tmp_path = make_unique_tmp_path(path);
    {
        std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            llvm::errs() << fmt::format("gentest_codegen: failed to open output file '{}'\n", tmp_path.string());
            return false;
        }
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.close();
        if (!out) {
            llvm::errs() << fmt::format("gentest_codegen: failed to write output file '{}'\n", tmp_path.string());
            return false;
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
            llvm::errs() << fmt::format("gentest_codegen: failed to replace output file '{}': {}\n", path.string(), ec.message());
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
        llvm::errs() << fmt::format("gentest_codegen: failed to create directory '{}': {}\n", path.parent_path().string(), ec.message());
        return false;
    }
    return true;
}

} // namespace

void replace_all(std::string &inout, std::string_view needle, std::string_view replacement) {
    const std::string target{needle};
    const std::string substitute{replacement};
    std::size_t       pos = 0;
    while ((pos = inout.find(target, pos)) != std::string::npos) {
        inout.replace(pos, target.size(), substitute);
        pos += substitute.size();
    }
}

auto render_cases(const CollectorOptions &options, const std::vector<TestCaseInfo> &cases) -> std::optional<std::string> {
    std::string template_content;
    if (!options.template_path.empty()) {
        template_content = render::read_template_file(options.template_path);
        if (template_content.empty()) {
            llvm::errs() << fmt::format("gentest_codegen: failed to load template file '{}', using built-in template.\n",
                                        options.template_path.string());
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
    const auto tpl_group_ephemeral   = std::string(tpl::group_runner_ephemeral);
    const auto tpl_group_suite       = std::string(tpl::group_runner_suite);
    const auto tpl_group_global      = std::string(tpl::group_runner_global);
    const auto tpl_array_empty       = std::string(tpl::array_decl_empty);
    const auto tpl_array_nonempty    = std::string(tpl::array_decl_nonempty);
    const auto tpl_fwd_line          = std::string(tpl::forward_decl_line);
    const auto tpl_fwd_ns            = std::string(tpl::forward_decl_ns);

    std::string forward_decl_block = render::render_forward_decls(cases, tpl_fwd_line, tpl_fwd_ns);

    auto                     traits                  = render::render_trait_arrays(cases, tpl_array_empty, tpl_array_nonempty);
    std::string              trait_declarations      = std::move(traits.declarations);
    std::vector<std::string> tag_array_names         = std::move(traits.tag_names);
    std::vector<std::string> requirement_array_names = std::move(traits.req_names);

    std::string wrapper_impls =
        render::render_wrappers(cases, tpl_wrapper_free, tpl_wrapper_free_fix, tpl_wrapper_ephemeral, tpl_wrapper_stateful);

    auto gr = render::render_groups(cases, tpl_group_ephemeral, tpl_group_suite, tpl_group_global);

    std::string case_entries;
    if (cases.empty()) {
        case_entries = "    // No test cases discovered during code generation.\n";
    } else {
        case_entries =
            render::render_case_entries(cases, tag_array_names, requirement_array_names, tpl_case_entry, gr.accessors);
    }

    std::string accessor_decls = std::move(gr.declarations);
    std::string group_runners  = std::move(gr.runners);
    std::string run_groups     = std::move(gr.run_calls);

    std::string output = template_content;
    replace_all(output, "{{FORWARD_DECLS}}", forward_decl_block);
    replace_all(output, "{{CASE_COUNT}}", std::to_string(cases.size()));
    replace_all(output, "{{TRAIT_DECLS}}", trait_declarations);
    replace_all(output, "{{WRAPPER_IMPLS}}", wrapper_impls);
    replace_all(output, "{{ACCESSOR_DECLS}}", accessor_decls);
    replace_all(output, "{{CASE_INITS}}", case_entries);
    replace_all(output, "{{ENTRY_FUNCTION}}", options.entry);
    // Version for --help
#if defined(GENTEST_VERSION_STR)
    replace_all(output, "{{VERSION}}", GENTEST_VERSION_STR);
#else
    replace_all(output, "{{VERSION}}", "0.0.0");
#endif
    replace_all(output, "{{GROUP_RUNNERS}}", group_runners);
    replace_all(output, "{{RUN_GROUPS}}", run_groups);

    const fs::path out_dir = options.output_path.has_parent_path() ? options.output_path.parent_path() : fs::current_path();
    std::string test_decls_include;
    if (!options.test_decls_path.empty()) {
        test_decls_include = fmt::format("#include \"{}\"\n", include_path_for_generated(out_dir, options.test_decls_path));
    }
    replace_all(output, "{{TEST_DECLS_INCLUDE}}", test_decls_include);

    // Mock registry and inline implementations are pulled via gentest/mock.h after
    // the generated declarations so user types are visible first.

    return output;
}

int emit(const CollectorOptions &opts, const std::vector<TestCaseInfo> &cases, const std::vector<MockClassInfo> &mocks) {
    fs::path out_path = opts.output_path;
    if (!ensure_parent_dir(out_path)) {
        return 1;
    }

    // Embedded template is used when no template path is provided.

    if (!opts.test_decls_path.empty()) {
        if (!ensure_parent_dir(opts.test_decls_path)) {
            return 1;
        }
        const auto decls = render_test_decls_header(opts, cases);
        if (!write_file_atomic_if_changed(opts.test_decls_path, decls)) {
            return 1;
        }
    }

    const auto content = render_cases(opts, cases);
    if (!content) {
        return 1;
    }

    if (!write_file_atomic_if_changed(out_path, *content)) {
        return 1;
    }

    const bool have_mock_paths = !opts.mock_registry_path.empty() && !opts.mock_impl_path.empty();
    if (!mocks.empty() || have_mock_paths) {
        if (!have_mock_paths) {
            llvm::errs() << "gentest_codegen: mock outputs requested but --mock-registry/--mock-impl paths were not provided\n";
            return 1;
        }
        auto                rendered = render::render_mocks(opts, mocks);
        render::MockOutputs outputs;
        if (rendered) {
            outputs = std::move(*rendered);
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
    }

    return 0;
}

} // namespace gentest::codegen
