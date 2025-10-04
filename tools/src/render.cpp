// Implementation of rendering helpers for templates

#include "render.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <string>

#include <fmt/core.h>

namespace gentest::codegen::render {

std::string read_template_file(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
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

std::string render_forward_decls(const std::vector<TestCaseInfo> &cases, const std::string &tpl_line, const std::string &tpl_ns) {
    std::map<std::string, std::set<std::string>> forward_decls;
    for (const auto &test : cases) {
        if (!test.fixture_qualified_name.empty()) continue;
        // Skip templated instantiations in forward decls (not valid syntax)
        if (test.qualified_name.find('<') != std::string::npos) continue;
        std::string scope;
        std::string basename = test.qualified_name;
        if (auto pos = basename.rfind("::"); pos != std::string::npos) {
            scope    = basename.substr(0, pos);
            basename = basename.substr(pos + 2);
        }
        forward_decls[scope].insert(basename);
    }

    std::string block;
    for (const auto &[scope, functions] : forward_decls) {
        std::string lines;
        for (const auto &name : functions) lines += fmt::format(fmt::runtime(tpl_line), fmt::arg("name", name));
        if (scope.empty()) {
            block += lines;
        } else {
            block += fmt::format(fmt::runtime(tpl_ns), fmt::arg("scope", scope), fmt::arg("lines", lines));
        }
    }
    return block;
}

static std::string format_sv_array(const std::string &name, const std::vector<std::string> &values, const std::string &tpl_empty,
                                   const std::string &tpl_nonempty) {
    if (values.empty()) {
        return fmt::format(fmt::runtime(tpl_empty), fmt::arg("name", name)) + std::string("\n");
    }
    std::string body;
    for (const auto &v : values) body += fmt::format("    \"{}\",\n", escape_string(v));
    return fmt::format(fmt::runtime(tpl_nonempty), fmt::arg("count", values.size()), fmt::arg("name", name), fmt::arg("body", body)) +
           std::string("\n");
}

TraitArrays render_trait_arrays(const std::vector<TestCaseInfo> &cases, const std::string &tpl_array_empty,
                                const std::string &tpl_array_nonempty) {
    TraitArrays out;
    for (std::size_t idx = 0; idx < cases.size(); ++idx) {
        const auto &test = cases[idx];
        std::string tag_name = "kTags_" + std::to_string(idx);
        std::string req_name = "kReqs_" + std::to_string(idx);
        out.tag_names.emplace_back(tag_name);
        out.req_names.emplace_back(req_name);
        out.declarations += format_sv_array(tag_name, test.tags, tpl_array_empty, tpl_array_nonempty);
        out.declarations += format_sv_array(req_name, test.requirements, tpl_array_empty, tpl_array_nonempty);
    }
    return out;
}

namespace {
// Small helpers to simplify wrapper emission and avoid inline string assembly
std::string build_fixture_decls(const std::vector<std::string>& types) {
    std::string decls;
    for (std::size_t i = 0; i < types.size(); ++i) {
        decls += fmt::format("    {} fx{}_{{}};\n", types[i], i);
    }
    return decls;
}

std::string build_fixture_setup(const std::vector<std::string>& types) {
    (void)types;
    std::string setup;
    for (std::size_t i = 0; i < types.size(); ++i) setup += fmt::format("    gentest_maybe_setup(fx{}_);\n", i);
    return setup;
}

std::string build_fixture_teardown(const std::vector<std::string>& types) {
    (void)types;
    std::string td;
    for (std::size_t i = types.size(); i-- > 0;) td += fmt::format("    gentest_maybe_teardown(fx{}_);\n", i);
    return td;
}

std::string build_fixture_arg_list(std::size_t count) {
    std::string args;
    for (std::size_t i = 0; i < count; ++i) { if (i) args += ", "; args += fmt::format("fx{}_", i); }
    return args;
}
} // namespace

std::string render_wrappers(const std::vector<TestCaseInfo> &cases, const std::string &tpl_free,
                            const std::string &tpl_free_fixtures, const std::string &tpl_ephemeral,
                            const std::string &tpl_stateful) {
    auto make_wrapper_name = [](std::size_t idx) { return std::string("kCaseInvoke_") + std::to_string(idx); };
    std::string out;
    out.reserve(cases.size() * 160);
    for (std::size_t idx = 0; idx < cases.size(); ++idx) {
        const auto &test = cases[idx];
        const auto  w    = make_wrapper_name(idx);
        auto args = test.call_arguments.empty() ? std::string() : test.call_arguments;
        if (test.fixture_qualified_name.empty()) {
            if (!test.free_fixtures.empty()) {
                const std::string decls    = build_fixture_decls(test.free_fixtures);
                const std::string setup    = build_fixture_setup(test.free_fixtures);
                const std::string teardown = build_fixture_teardown(test.free_fixtures);
                std::string       combined = build_fixture_arg_list(test.free_fixtures.size());
                if (!args.empty()) combined += combined.empty() ? args : ", " + args;
                const std::string call = fmt::format("({})", combined);
                out += fmt::format(fmt::runtime(tpl_free_fixtures), fmt::arg("w", w), fmt::arg("fn", test.qualified_name),
                                   fmt::arg("decls", decls), fmt::arg("setup", setup), fmt::arg("teardown", teardown),
                                   fmt::arg("call", call));
            } else {
                auto call = args.empty() ? std::string("()") : fmt::format("({})", args);
                out += fmt::format(fmt::runtime(tpl_free), fmt::arg("w", w), fmt::arg("fn", test.qualified_name), fmt::arg("args", call));
            }
        } else {
            std::string method_name = test.qualified_name;
            if (auto pos = method_name.rfind("::"); pos != std::string::npos) method_name = method_name.substr(pos + 2);
            if (test.fixture_stateful) {
                auto call = args.empty() ? std::string("()") : fmt::format("({})", args);
                out += fmt::format(fmt::runtime(tpl_stateful), fmt::arg("w", w), fmt::arg("fixture", test.fixture_qualified_name),
                                   fmt::arg("method", method_name), fmt::arg("args", call));
            } else {
                auto call = args.empty() ? std::string("()") : fmt::format("({})", args);
                out += fmt::format(fmt::runtime(tpl_ephemeral), fmt::arg("w", w), fmt::arg("fixture", test.fixture_qualified_name),
                                   fmt::arg("method", method_name), fmt::arg("args", call));
            }
        }
    }
    return out;
}

std::string render_case_entries(const std::vector<TestCaseInfo> &cases, const std::vector<std::string> &tag_names,
                                const std::vector<std::string> &req_names, const std::string &tpl_case_entry) {
    std::string out;
    out.reserve(cases.size() * 128);
    for (std::size_t idx = 0; idx < cases.size(); ++idx) {
        const auto &test = cases[idx];
        out += fmt::format(fmt::runtime(tpl_case_entry), fmt::arg("name", escape_string(test.display_name)),
                           fmt::arg("wrapper", std::string("kCaseInvoke_") + std::to_string(idx)),
                           fmt::arg("file", escape_string(test.filename)), fmt::arg("line", test.line),
                           fmt::arg("tags", tag_names[idx]), fmt::arg("reqs", req_names[idx]),
                           fmt::arg("skip_reason", !test.skip_reason.empty() ? "\"" + escape_string(test.skip_reason) + "\""
                                                                         : std::string("std::string_view{}")),
                           fmt::arg("should_skip", test.should_skip ? "true" : "false"),
                           fmt::arg("fixture", !test.fixture_qualified_name.empty() ? "\"" + escape_string(test.fixture_qualified_name) + "\""
                                                                               : std::string("std::string_view{}")),
                           fmt::arg("stateful", test.fixture_stateful ? "true" : "false"));
    }
    return out;
}

GroupRender render_groups(const std::vector<TestCaseInfo> &cases, const std::string &tpl_stateless, const std::string &tpl_stateful) {
    std::map<std::string, std::vector<std::size_t>> groups;
    for (std::size_t idx = 0; idx < cases.size(); ++idx) {
        const auto &test = cases[idx];
        if (test.fixture_qualified_name.empty()) continue;
        groups[test.fixture_qualified_name].push_back(idx);
    }
    GroupRender out;
    std::size_t gid = 0;
    for (const auto &kv : groups) {
        const auto &fixture = kv.first;
        const auto &idxs    = kv.second;
        if (idxs.empty()) continue;
        bool stateful = std::any_of(idxs.begin(), idxs.end(), [&](std::size_t i) { return cases[i].fixture_stateful; });
        std::string idx_list;
        for (std::size_t j = 0; j < idxs.size(); ++j) {
            if (j != 0) idx_list += ", ";
            idx_list += std::to_string(idxs[j]);
        }
        out.runners += fmt::format(fmt::runtime(stateful ? tpl_stateful : tpl_stateless), fmt::arg("gid", gid), fmt::arg("fixture", fixture),
                                   fmt::arg("count", idxs.size()), fmt::arg("idxs", idx_list));
    out.run_calls += fmt::format("    gentest_run_group_{gid}(shuffle, seed, counters);\n", fmt::arg("gid", gid));
        ++gid;
    }
    return out;
}

} // namespace gentest::codegen::render
