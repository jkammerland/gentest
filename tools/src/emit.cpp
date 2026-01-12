// Implementation of template-based emission for test cases

#include "emit.hpp"

#include "render.hpp"
#include "render_mocks.hpp"
#include "templates.hpp"

#include <chrono>
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

std::string include_path_for_source(const CollectorOptions &options, const fs::path &out_dir, const fs::path &source_path) {
    const fs::path abs_src = normalize_path(source_path);
    for (const auto &root : options.include_roots) {
        if (root.empty()) {
            continue;
        }
        const fs::path abs_root = normalize_path(root);
        fs::path       rel      = abs_src.lexically_relative(abs_root);
        if (rel.empty() || rel.is_absolute()) {
            continue;
        }
        if (rel.begin() != rel.end() && *rel.begin() == "..") {
            continue;
        }
        return rel.generic_string();
    }

    // Fallback: emit a relative path to the generated output directory to
    // avoid absolute includes in generated code.
    fs::path rel = abs_src.lexically_relative(normalize_path(out_dir));
    if (rel.empty() || rel.is_absolute()) {
        rel = source_path;
    }
    if (rel.is_absolute()) {
        rel = source_path.filename();
    }
    return rel.generic_string();
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

    // Include sources in the generated file so fixture types are visible
    std::string    includes;
    const bool     skip_includes = !options.include_sources;
    const fs::path out_dir = options.output_path.has_parent_path() ? options.output_path.parent_path() : fs::current_path();
    for (const auto &src : options.sources) {
        if (skip_includes) break;
        const fs::path spath(src);
        includes += fmt::format("#include \"{}\"\n", include_path_for_source(options, out_dir, spath));
    }
    replace_all(output, "{{INCLUDE_SOURCES}}", includes);

    // Mock registry and inline implementations are pulled via gentest/mock.h in the
    // template after including sources, ensuring original types are visible first.

    return output;
}

int emit(const CollectorOptions &opts, const std::vector<TestCaseInfo> &cases, const std::vector<MockClassInfo> &mocks) {
    fs::path out_path = opts.output_path;
    if (!ensure_parent_dir(out_path)) {
        return 1;
    }

    // Embedded template is used when no template path is provided.

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
