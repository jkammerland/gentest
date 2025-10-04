// Implementation of template-based emission for test cases

#include "emit.hpp"

#include "render.hpp"
#include "templates.hpp"

#include <filesystem>
#include <fmt/core.h>
#include <fstream>
#include <llvm/Support/raw_ostream.h>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace gentest::codegen {

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
    const auto tpl_group_stateless   = std::string(tpl::group_runner_stateless);
    const auto tpl_group_stateful    = std::string(tpl::group_runner_stateful);
    const auto tpl_array_empty       = std::string(tpl::array_decl_empty);
    const auto tpl_array_nonempty    = std::string(tpl::array_decl_nonempty);
    const auto tpl_fwd_line          = std::string(tpl::forward_decl_line);
    const auto tpl_fwd_ns            = std::string(tpl::forward_decl_ns);

    std::string forward_decl_block = render::render_forward_decls(cases, tpl_fwd_line, tpl_fwd_ns);

    auto                     traits                  = render::render_trait_arrays(cases, tpl_array_empty, tpl_array_nonempty);
    std::string              trait_declarations      = std::move(traits.declarations);
    std::vector<std::string> tag_array_names         = std::move(traits.tag_names);
    std::vector<std::string> requirement_array_names = std::move(traits.req_names);

    std::string wrapper_impls = render::render_wrappers(cases, tpl_wrapper_free, tpl_wrapper_free_fix, tpl_wrapper_ephemeral, tpl_wrapper_stateful);

    std::string case_entries;
    if (cases.empty()) {
        case_entries = "    // No test cases discovered during code generation.\n";
    } else {
        case_entries = render::render_case_entries(cases, tag_array_names, requirement_array_names, tpl_case_entry);
    }

    auto        gr            = render::render_groups(cases, tpl_group_stateless, tpl_group_stateful);
    std::string group_runners = std::move(gr.runners);
    std::string run_groups    = std::move(gr.run_calls);

    std::string output = template_content;
    replace_all(output, "{{FORWARD_DECLS}}", forward_decl_block);
    replace_all(output, "{{CASE_COUNT}}", std::to_string(cases.size()));
    replace_all(output, "{{TRAIT_DECLS}}", trait_declarations);
    replace_all(output, "{{WRAPPER_IMPLS}}", wrapper_impls);
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
    namespace fs = std::filesystem;
    std::string    includes;
    const fs::path out_dir = options.output_path.has_parent_path() ? options.output_path.parent_path() : fs::current_path();
    for (const auto &src : options.sources) {
        fs::path        spath(src);
        std::error_code ec;
        fs::path        rel = fs::proximate(spath, out_dir, ec);
        if (ec)
            rel = spath;
        includes += fmt::format("#include \"{}\"\n", rel.generic_string());
    }
    replace_all(output, "{{INCLUDE_SOURCES}}", includes);

    return output;
}

int emit(const CollectorOptions &opts, const std::vector<TestCaseInfo> &cases) {
    namespace fs      = std::filesystem;
    fs::path out_path = opts.output_path;
    if (out_path.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(out_path.parent_path(), ec);
        if (ec) {
            llvm::errs() << fmt::format("gentest_codegen: failed to create directory '{}': {}\n", out_path.parent_path().string(),
                                        ec.message());
            return 1;
        }
    }

    // Embedded template is used when no template path is provided.

    const auto content = render_cases(opts, cases);
    if (!content) {
        return 1;
    }

    std::ofstream file(out_path, std::ios::binary);
    if (!file) {
        llvm::errs() << fmt::format("gentest_codegen: failed to open output file '{}'\n", out_path.string());
        return 1;
    }
    file << *content;
    file.close();
    return file ? 0 : 1;
}

} // namespace gentest::codegen
