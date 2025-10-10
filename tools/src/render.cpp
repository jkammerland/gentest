// Implementation of rendering helpers for templates

#include "render.hpp"

#include <algorithm>
#include <fmt/core.h>
#include <fstream>
#include <map>
#include <set>
#include <string>

namespace gentest::codegen::render {

std::string read_template_file(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return {};
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

std::string render_forward_decls(const std::vector<TestCaseInfo> & /*cases*/, const std::string & /*tpl_line*/, const std::string & /*tpl_ns*/) {
    // Forward declarations for test functions are not emitted.
    // The generated TU includes the test sources before wrappers, so declarations
    // are available, and emitting prototypes with a fixed return type (e.g., void)
    // would incorrectly reject non-void test functions.
    return {};
}

static std::string format_sv_array(const std::string &name, const std::vector<std::string> &values, const std::string &tpl_empty,
                                   const std::string &tpl_nonempty) {
    if (values.empty()) {
        return fmt::format(fmt::runtime(tpl_empty), fmt::arg("name", name)) + std::string("\n");
    }
    std::string body;
    for (const auto &v : values)
        body += fmt::format("    \"{}\",\n", escape_string(v));
    return fmt::format(fmt::runtime(tpl_nonempty), fmt::arg("count", values.size()), fmt::arg("name", name), fmt::arg("body", body)) +
           std::string("\n");
}

TraitArrays render_trait_arrays(const std::vector<TestCaseInfo> &cases, const std::string &tpl_array_empty,
                                const std::string &tpl_array_nonempty) {
    TraitArrays out;
    for (std::size_t idx = 0; idx < cases.size(); ++idx) {
        const auto &test     = cases[idx];
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
std::string fixture_lifetime_literal(FixtureLifetime lt) {
    switch (lt) {
    case FixtureLifetime::None: return "gentest::FixtureLifetime::None";
    case FixtureLifetime::MemberEphemeral: return "gentest::FixtureLifetime::MemberEphemeral";
    case FixtureLifetime::MemberSuite: return "gentest::FixtureLifetime::MemberSuite";
    case FixtureLifetime::MemberGlobal: return "gentest::FixtureLifetime::MemberGlobal";
    }
    return "gentest::FixtureLifetime::None";
}

// Small helpers to simplify wrapper emission and avoid inline string assembly
enum class WrapperKind { Free, FreeWithFixtures, MemberEphemeral, MemberShared };

struct WrapperSpec {
    WrapperKind              kind;
    std::string              wrapper_name; // kCaseInvoke_N
    std::string              callee;       // free function (qualified) or fixture type (qualified)
    std::string              method;       // member method name (unqualified)
    std::vector<std::string> fixtures;     // for FreeWithFixtures
    std::string              value_args;   // comma-separated value args (may be empty)
    bool                     returns_value = false; // whether to capture result
};
std::string build_fixture_decls(const std::vector<std::string> &types) {
    std::string decls;
    for (std::size_t i = 0; i < types.size(); ++i) {
        decls += fmt::format("    {} fx{}_{{}};\n", types[i], i);
    }
    return decls;
}

std::string build_fixture_setup(const std::vector<std::string> &types) {
    (void)types;
    std::string setup;
    for (std::size_t i = 0; i < types.size(); ++i)
        setup += fmt::format("    gentest_maybe_setup(fx{}_);\n", i);
    return setup;
}

std::string build_fixture_teardown(const std::vector<std::string> &types) {
    (void)types;
    std::string td;
    for (std::size_t i = types.size(); i-- > 0;)
        td += fmt::format("    gentest_maybe_teardown(fx{}_);\n", i);
    return td;
}

std::string build_fixture_arg_list(std::size_t count) {
    std::string args;
    for (std::size_t i = 0; i < count; ++i) {
        if (i)
            args += ", ";
        args += fmt::format("fx{}_", i);
    }
    return args;
}

std::string extract_method_name(std::string qualified) {
    if (auto pos = qualified.rfind("::"); pos != std::string::npos)
        return qualified.substr(pos + 2);
    return qualified;
}

std::string format_call_args(const std::string &value_args) {
    return value_args.empty() ? std::string("()") : fmt::format("({})", value_args);
}

static std::string make_invoke_for_free(const WrapperSpec &spec, const std::string &fn, const std::string &args) {
    if (spec.returns_value) {
        return fmt::format("[[maybe_unused]] const auto _ = {}{};", fn, args);
    }
    return fmt::format("static_cast<void>({}{});", fn, args);
}

static std::string make_invoke_for_member(const WrapperSpec &spec, const std::string &call_expr) {
    if (spec.returns_value) {
        return fmt::format("[[maybe_unused]] const auto _ = {};", call_expr);
    }
    return fmt::format("static_cast<void>({});", call_expr);
}

std::string render_wrapper(const WrapperSpec &spec, const std::string &tpl_free, const std::string &tpl_free_fixtures,
                           const std::string &tpl_ephemeral, const std::string &tpl_stateful) {
    switch (spec.kind) {
    case WrapperKind::Free: {
        const auto call = format_call_args(spec.value_args);
        const auto invoke = make_invoke_for_free(spec, spec.callee, call);
        return fmt::format(fmt::runtime(tpl_free), fmt::arg("w", spec.wrapper_name), fmt::arg("invoke", invoke));
    }
    case WrapperKind::FreeWithFixtures: {
        const std::string decls    = build_fixture_decls(spec.fixtures);
        const std::string setup    = build_fixture_setup(spec.fixtures);
        const std::string teardown = build_fixture_teardown(spec.fixtures);
        std::string       combined = build_fixture_arg_list(spec.fixtures.size());
        if (!spec.value_args.empty())
            combined += combined.empty() ? spec.value_args : ", " + spec.value_args;
        const std::string call = fmt::format("({})", combined);
        const auto invoke = make_invoke_for_free(spec, spec.callee, call);
        return fmt::format(fmt::runtime(tpl_free_fixtures), fmt::arg("w", spec.wrapper_name), fmt::arg("decls", decls),
                           fmt::arg("setup", setup), fmt::arg("teardown", teardown), fmt::arg("invoke", invoke));
    }
    case WrapperKind::MemberEphemeral: {
        const auto call = format_call_args(spec.value_args);
        const auto call_expr = fmt::format("fx_.{}{}", spec.method, call);
        const auto invoke    = make_invoke_for_member(spec, call_expr);
        return fmt::format(fmt::runtime(tpl_ephemeral), fmt::arg("w", spec.wrapper_name), fmt::arg("fixture", spec.callee),
                           fmt::arg("invoke", invoke));
    }
    case WrapperKind::MemberShared: {
        const auto call = format_call_args(spec.value_args);
        const auto call_expr = fmt::format("fx_->{}{}", spec.method, call);
        const auto invoke    = make_invoke_for_member(spec, call_expr);
        return fmt::format(fmt::runtime(tpl_stateful), fmt::arg("w", spec.wrapper_name), fmt::arg("fixture", spec.callee),
                           fmt::arg("invoke", invoke));
    }
    }
    return {};
}

WrapperSpec build_wrapper_spec(const TestCaseInfo &test, std::size_t idx) {
    WrapperSpec spec{};
    spec.wrapper_name = std::string("kCaseInvoke_") + std::to_string(idx);
    spec.value_args   = test.call_arguments; // may be empty
    if (test.fixture_qualified_name.empty()) {
        if (!test.free_fixtures.empty()) {
            spec.kind     = WrapperKind::FreeWithFixtures;
            spec.callee   = test.qualified_name;
            spec.fixtures = test.free_fixtures;
        } else {
            spec.kind   = WrapperKind::Free;
            spec.callee = test.qualified_name;
        }
    } else {
        switch (test.fixture_lifetime) {
        case FixtureLifetime::MemberSuite:
        case FixtureLifetime::MemberGlobal: spec.kind = WrapperKind::MemberShared; break;
        case FixtureLifetime::MemberEphemeral:
        case FixtureLifetime::None:
        default: spec.kind = WrapperKind::MemberEphemeral; break;
        }
        spec.callee = test.fixture_qualified_name;
        spec.method = extract_method_name(test.qualified_name);
    }
    spec.returns_value = test.returns_value;
    return spec;
}
} // namespace

std::string render_wrappers(const std::vector<TestCaseInfo> &cases, const std::string &tpl_free, const std::string &tpl_free_fixtures,
                            const std::string &tpl_ephemeral, const std::string &tpl_stateful) {
    std::string out;
    out.reserve(cases.size() * 160);
    for (std::size_t idx = 0; idx < cases.size(); ++idx) {
        const auto &test = cases[idx];
        const auto  spec = build_wrapper_spec(test, idx);
        out += render_wrapper(spec, tpl_free, tpl_free_fixtures, tpl_ephemeral, tpl_stateful);
    }
    return out;
}

std::string render_case_entries(const std::vector<TestCaseInfo> &cases, const std::vector<std::string> &tag_names,
                                const std::vector<std::string> &req_names, const std::string &tpl_case_entry,
                                const std::map<std::string, std::string> &fixture_accessors) {
    std::string out;
    out.reserve(cases.size() * 128);
    for (std::size_t idx = 0; idx < cases.size(); ++idx) {
        const auto &test             = cases[idx];
        std::string accessor_literal = "nullptr";
        if (!test.fixture_qualified_name.empty()) {
            if (auto it = fixture_accessors.find(test.fixture_qualified_name); it != fixture_accessors.end()) {
                accessor_literal = it->second;
            }
        }

        out += fmt::format(
            fmt::runtime(tpl_case_entry), fmt::arg("name", escape_string(test.display_name)),
            fmt::arg("wrapper", std::string("kCaseInvoke_") + std::to_string(idx)), fmt::arg("file", escape_string(test.filename)),
            fmt::arg("line", test.line), fmt::arg("tags", tag_names[idx]), fmt::arg("reqs", req_names[idx]),
            fmt::arg("skip_reason",
                     !test.skip_reason.empty() ? "\"" + escape_string(test.skip_reason) + "\"" : std::string("std::string_view{}")),
            fmt::arg("should_skip", test.should_skip ? "true" : "false"),
            fmt::arg("fixture", !test.fixture_qualified_name.empty() ? "\"" + escape_string(test.fixture_qualified_name) + "\""
                                                                     : std::string("std::string_view{}")),
            fmt::arg("lifetime", fixture_lifetime_literal(test.fixture_lifetime)),
            fmt::arg("suite", !test.suite_name.empty() ? "\"" + escape_string(test.suite_name) + "\"" : std::string("std::string_view{}")),
            fmt::arg("acquire", accessor_literal));
    }
    return out;
}

GroupRender render_groups(const std::vector<TestCaseInfo> &cases, const std::string &tpl_ephemeral, const std::string &tpl_suite,
                          const std::string &tpl_global) {
    std::map<std::string, std::vector<std::size_t>> groups;
    for (std::size_t idx = 0; idx < cases.size(); ++idx) {
        const auto &test = cases[idx];
        if (test.fixture_qualified_name.empty())
            continue;
        groups[test.fixture_qualified_name].push_back(idx);
    }
    GroupRender out;
    std::size_t gid = 0;
    for (const auto &kv : groups) {
        const auto &fixture = kv.first;
        const auto &idxs    = kv.second;
        if (idxs.empty())
            continue;
        FixtureLifetime lifetime = FixtureLifetime::MemberEphemeral;
        if (const auto first = idxs.front(); first < cases.size()) {
            lifetime = cases[first].fixture_lifetime;
        }
        std::string idx_list;
        for (std::size_t j = 0; j < idxs.size(); ++j) {
            if (j != 0)
                idx_list += ", ";
            idx_list += std::to_string(idxs[j]);
        }
        const std::string *tpl              = &tpl_ephemeral;
        std::string        accessor_literal = "nullptr";
        switch (lifetime) {
        case FixtureLifetime::MemberSuite:
            tpl              = &tpl_suite;
            accessor_literal = std::string("&gentest_access_fixture_") + std::to_string(gid);
            out.declarations += fmt::format("static void* gentest_access_fixture_{gid}(std::string_view);\n",
                                             fmt::arg("gid", gid));
            break;
        case FixtureLifetime::MemberGlobal:
            tpl              = &tpl_global;
            accessor_literal = std::string("&gentest_access_fixture_") + std::to_string(gid);
            out.declarations += fmt::format("static void* gentest_access_fixture_{gid}(std::string_view);\n",
                                             fmt::arg("gid", gid));
            break;
        case FixtureLifetime::MemberEphemeral:
        case FixtureLifetime::None:
        default: tpl = &tpl_ephemeral; break;
        }
        out.runners += fmt::format(fmt::runtime(*tpl), fmt::arg("gid", gid), fmt::arg("fixture", fixture), fmt::arg("count", idxs.size()),
                                   fmt::arg("idxs", idx_list));
        out.run_calls += fmt::format("    gentest_run_group_{gid}(shuffle, seed, counters);\n", fmt::arg("gid", gid));
        out.accessors[fixture] = accessor_literal;
        ++gid;
    }
    return out;
}

} // namespace gentest::codegen::render
