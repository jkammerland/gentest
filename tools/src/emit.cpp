// Implementation of template-based emission for test cases

#include "emit.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include <llvm/Support/raw_ostream.h>
#include <fmt/core.h>
#include <fmt/core.h>

namespace gentest::codegen {

std::string read_template_file(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void replace_all(std::string &inout, std::string_view needle, std::string_view replacement) {
    const std::string target{needle};
    const std::string substitute{replacement};
    std::size_t       pos = 0;
    while ((pos = inout.find(target, pos)) != std::string::npos) {
        inout.replace(pos, target.size(), substitute);
        pos += substitute.size();
    }
}

std::string escape_string(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char ch : value) {
        switch (ch) {
        case '\\': escaped += "\\\\"; break;
        case '\"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped.push_back(ch); break;
        }
    }
    return escaped;
}

auto render_cases(const CollectorOptions &options, const std::vector<TestCaseInfo> &cases) -> std::optional<std::string> {
    const auto template_content = read_template_file(options.template_path);
    if (template_content.empty()) {
        llvm::errs() << fmt::format("gentest_codegen: failed to load template file '{}'\n", options.template_path.string());
        return std::nullopt;
    }

    // Forward declarations for free functions (not strictly needed when we include sources,
    // but harmless and keeps template consistent)
    std::map<std::string, std::set<std::string>> forward_decls;
    for (const auto &test : cases) {
        if (!test.fixture_qualified_name.empty()) continue; // skip methods
        std::string scope;
        std::string basename = test.qualified_name;
        if (auto pos = basename.rfind("::"); pos != std::string::npos) {
            scope    = basename.substr(0, pos);
            basename = basename.substr(pos + 2);
        }
        forward_decls[scope].insert(basename);
    }

    std::string forward_decl_block;
    for (const auto &[scope, functions] : forward_decls) {
        std::string lines;
        for (const auto &name : functions) {
            lines += fmt::format("void {}();\n", name);
        }
        if (scope.empty()) {
            forward_decl_block += lines;
        } else {
            forward_decl_block += fmt::format("namespace {} {{\n{}}} // namespace {}\n", scope, lines, scope);
        }
    }

    std::vector<std::string> tag_array_names;
    std::vector<std::string> requirement_array_names;
    tag_array_names.reserve(cases.size());
    requirement_array_names.reserve(cases.size());

    auto format_sv_array = [&](const std::string &name, const std::vector<std::string> &values) {
        if (values.empty()) {
            return fmt::format("constexpr std::array<std::string_view, 0> {}{};\n\n", name, "{}");
        }
        std::string body;
        for (const auto &v : values) {
            body += fmt::format("    \"{}\",\n", escape_string(v));
        }
        return fmt::format("constexpr std::array<std::string_view, {count}> {name} = {{\n{body}}};\n\n",
                           fmt::arg("count", values.size()), fmt::arg("name", name), fmt::arg("body", body));
    };

    std::string trait_declarations;
    for (std::size_t idx = 0; idx < cases.size(); ++idx) {
        const auto &test = cases[idx];
        std::string tag_name = "kTags_" + std::to_string(idx);
        std::string req_name = "kReqs_" + std::to_string(idx);
        tag_array_names.emplace_back(tag_name);
        requirement_array_names.emplace_back(req_name);

        trait_declarations += format_sv_array(tag_name, test.tags);
        trait_declarations += format_sv_array(req_name, test.requirements);
    }

    // Build wrapper implementations for all cases (free and member tests)
    std::string wrapper_impls;
    wrapper_impls.reserve(cases.size() * 160);

    auto make_wrapper_name = [](std::size_t idx) { return std::string("kCaseInvoke_") + std::to_string(idx); };

    for (std::size_t idx = 0; idx < cases.size(); ++idx) {
        const auto &test = cases[idx];
        const auto  w    = make_wrapper_name(idx);

        if (test.fixture_qualified_name.empty()) {
            wrapper_impls += fmt::format(
                R"(static void {w}(void* __ctx) {{
    (void)__ctx;
    {fn}();
}}

)",
                fmt::arg("w", w), fmt::arg("fn", test.qualified_name));
        } else {
            std::string method_name = test.qualified_name;
            if (auto pos = method_name.rfind("::"); pos != std::string::npos) {
                method_name = method_name.substr(pos + 2);
            }
            if (test.fixture_stateful) {
                wrapper_impls += fmt::format(
                    R"(static void {w}(void* __ctx) {{
    auto* __fx = static_cast<{fixture}*>(__ctx);
    __gentest_maybe_setup(*__fx);
    __fx->{method}();
    __gentest_maybe_teardown(*__fx);
}}

)",
                    fmt::arg("w", w), fmt::arg("fixture", test.fixture_qualified_name), fmt::arg("method", method_name));
            } else {
                wrapper_impls += fmt::format(
                    R"(static void {w}(void* __ctx) {{
    (void)__ctx;
    {fixture} __fx;
    __gentest_maybe_setup(__fx);
    __fx.{method}();
    __gentest_maybe_teardown(__fx);
}}

)",
                    fmt::arg("w", w), fmt::arg("fixture", test.fixture_qualified_name), fmt::arg("method", method_name));
            }
        }
    }

    std::string case_entries;
    if (cases.empty()) {
        case_entries = "    // No test cases discovered during code generation.\n";
    } else {
        case_entries.reserve(cases.size() * 128);
        for (std::size_t idx = 0; idx < cases.size(); ++idx) {
            const auto &test = cases[idx];
            const auto  entry = fmt::format(
                R"(    Case{{
        "{name}",
        &{wrapper},
        "{file}",
        {line},
        std::span{{{tags}}},
        std::span{{{reqs}}},
        {skip_reason},
        {should_skip},
        {fixture},
        {stateful}
    }},
)",
                fmt::arg("name", escape_string(test.display_name)),
                fmt::arg("wrapper", make_wrapper_name(idx)),
                fmt::arg("file", escape_string(test.filename)),
                fmt::arg("line", test.line),
                fmt::arg("tags", tag_array_names[idx]),
                fmt::arg("reqs", requirement_array_names[idx]),
                fmt::arg("skip_reason", !test.skip_reason.empty() ? "\"" + escape_string(test.skip_reason) + "\"" : std::string("std::string_view{}")),
                fmt::arg("should_skip", test.should_skip ? "true" : "false"),
                fmt::arg("fixture", !test.fixture_qualified_name.empty() ? "\"" + escape_string(test.fixture_qualified_name) + "\"" : std::string("std::string_view{}")),
                fmt::arg("stateful", test.fixture_stateful ? "true" : "false"));
            case_entries += entry;
        }
    }

    // Build per-fixture groups and corresponding runner functions
    std::map<std::string, std::vector<std::size_t>> groups;
    for (std::size_t idx = 0; idx < cases.size(); ++idx) {
        const auto &test = cases[idx];
        if (test.fixture_qualified_name.empty()) continue;
        groups[test.fixture_qualified_name].push_back(idx);
    }

    std::string group_runners;
    std::string run_groups;
    std::size_t gid = 0;
    for (const auto &kv : groups) {
        const auto &fixture = kv.first;
        const auto &idxs    = kv.second;
        if (idxs.empty()) continue;
        bool stateful = false;
        for (auto i : idxs) if (cases[i].fixture_stateful) { stateful = true; break; }
        std::string idx_list;
        for (std::size_t j = 0; j < idxs.size(); ++j) {
            if (j != 0) idx_list += ", ";
            idx_list += std::to_string(idxs[j]);
        }
        group_runners += fmt::format(
            R"(static void __gentest_run_group_{gid}(bool __shuffle, std::uint64_t __seed, Counters& __c) {{
    using Fixture = {fixture};
    const std::array<std::size_t, {count}> __idxs = {{{idxs}}};
    std::vector<std::size_t> __order(__idxs.begin(), __idxs.end());
    if (__shuffle && __order.size() > 1) {{ std::mt19937_64 __rng(__seed ? (__seed + {gid}) : std::mt19937_64::result_type(std::random_device{{}}())); std::shuffle(__order.begin(), __order.end(), __rng); }}
{body}
}}

)",
            fmt::arg("gid", gid), fmt::arg("fixture", fixture), fmt::arg("count", idxs.size()), fmt::arg("idxs", idx_list),
            fmt::arg("body", stateful ? std::string("    Fixture __fx;\n    for (auto __i : __order) { execute_one(kCases[__i], &__fx, __c); }\n")
                                       : std::string("    for (auto __i : __order) { execute_one(kCases[__i], nullptr, __c); }\n")));

        run_groups += fmt::format("    __gentest_run_group_{gid}(shuffle, seed, counters);\n", fmt::arg("gid", gid));
        ++gid;
    }

    std::string output = template_content;
    replace_all(output, "{{FORWARD_DECLS}}", forward_decl_block);
    replace_all(output, "{{CASE_COUNT}}", std::to_string(cases.size()));
    replace_all(output, "{{TRAIT_DECLS}}", trait_declarations);
    replace_all(output, "{{WRAPPER_IMPLS}}", wrapper_impls);
    replace_all(output, "{{CASE_INITS}}", case_entries);
    replace_all(output, "{{ENTRY_FUNCTION}}", options.entry);
    replace_all(output, "{{GROUP_RUNNERS}}", group_runners);
    replace_all(output, "{{RUN_GROUPS}}", run_groups);

    // Include sources in the generated file so fixture types are visible
    namespace fs = std::filesystem;
    std::string includes;
    const fs::path out_dir = options.output_path.has_parent_path() ? options.output_path.parent_path() : fs::current_path();
    for (const auto &src : options.sources) {
        fs::path spath(src);
        std::error_code ec;
        fs::path rel = fs::proximate(spath, out_dir, ec);
        if (ec) rel = spath;
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
            llvm::errs() << fmt::format("gentest_codegen: failed to create directory '{}': {}\n", out_path.parent_path().string(), ec.message());
            return 1;
        }
    }

    if (opts.template_path.empty()) {
        llvm::errs() << fmt::format("gentest_codegen: no template path configured\n");
        return 1;
    }

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
