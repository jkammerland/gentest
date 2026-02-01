// Implementation of rendering helpers for templates

#include "render.hpp"

#include <algorithm>
#include <fmt/format.h>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>

namespace gentest::codegen::render {

std::string read_template_file(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return {};
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
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

namespace {
template <typename... Args>
inline void append_format(std::string &out, fmt::format_string<Args...> format_string, Args &&...args) {
    fmt::format_to(std::back_inserter(out), format_string, std::forward<Args>(args)...);
}

template <typename... Args>
inline void append_format_runtime(std::string &out, const std::string &format_string, Args &&...args) {
    fmt::format_to(std::back_inserter(out), fmt::runtime(format_string), std::forward<Args>(args)...);
}
} // namespace

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static std::string format_sv_array(const std::string &name, const std::vector<std::string> &values, const std::string &tpl_empty,
                                   const std::string &tpl_nonempty) {
    if (values.empty()) {
        std::string out;
        out.reserve(tpl_empty.size() + name.size() + 4);
        append_format_runtime(out, tpl_empty, fmt::arg("name", name));
        out.push_back('\n');
        return out;
    }
    std::string body;
    body.reserve(values.size() * 16);
    for (const auto &v : values)
        append_format(body, "    \"{}\",\n", escape_string(v));
    std::string out;
    out.reserve(tpl_nonempty.size() + body.size() + 32);
    append_format_runtime(out, tpl_nonempty, fmt::arg("count", values.size()), fmt::arg("name", name), fmt::arg("body", body));
    out.push_back('\n');
    return out;
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
    decls.reserve(types.size() * 24);
    for (std::size_t i = 0; i < types.size(); ++i) {
        append_format(decls, "    {} fx{}_{{}};\n", types[i], i);
    }
    return decls;
}

std::string build_fixture_setup(const std::vector<std::string> &types) {
    (void)types;
    std::string setup;
    setup.reserve(types.size() * 28);
    for (std::size_t i = 0; i < types.size(); ++i)
        append_format(setup, "    gentest_maybe_setup(fx{}_);\n", i);
    return setup;
}

std::string build_fixture_teardown(const std::vector<std::string> &types) {
    (void)types;
    std::string td;
    td.reserve(types.size() * 30);
    for (std::size_t i = types.size(); i-- > 0;)
        append_format(td, "    gentest_maybe_teardown(fx{}_);\n", i);
    return td;
}

std::string build_fixture_arg_list(std::size_t count) {
    std::string args;
    args.reserve(count * 6);
    for (std::size_t i = 0; i < count; ++i) {
        if (i)
            args += ", ";
        append_format(args, "fx{}_", i);
    }
    return args;
}

std::string extract_method_name(std::string qualified) {
    if (auto pos = qualified.rfind("::"); pos != std::string::npos)
        return qualified.substr(pos + 2);
    return qualified;
}

std::string format_call_args(const std::string &value_args) {
    if (value_args.empty())
        return std::string("()");
    std::string out;
    out.reserve(value_args.size() + 2);
    out.push_back('(');
    out.append(value_args);
    out.push_back(')');
    return out;
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

static void append_wrapper(std::string &out, const WrapperSpec &spec, const WrapperTemplates &templates) {
    switch (spec.kind) {
    case WrapperKind::Free: {
        const auto call = format_call_args(spec.value_args);
        const auto invoke = make_invoke_for_free(spec, spec.callee, call);
        append_format_runtime(out, templates.free, fmt::arg("w", spec.wrapper_name), fmt::arg("invoke", invoke));
        return;
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
        append_format_runtime(out, templates.free_fixtures, fmt::arg("w", spec.wrapper_name), fmt::arg("decls", decls),
                              fmt::arg("setup", setup), fmt::arg("teardown", teardown), fmt::arg("invoke", invoke));
        return;
    }
    case WrapperKind::MemberEphemeral: {
        const auto call = format_call_args(spec.value_args);
        const auto call_expr = fmt::format("fx_.{}{}", spec.method, call);
        const auto invoke    = make_invoke_for_member(spec, call_expr);
        append_format_runtime(out, templates.ephemeral, fmt::arg("w", spec.wrapper_name), fmt::arg("fixture", spec.callee),
                              fmt::arg("invoke", invoke));
        return;
    }
    case WrapperKind::MemberShared: {
        const auto call = format_call_args(spec.value_args);
        const auto call_expr = fmt::format("fx_->{}{}", spec.method, call);
        const auto invoke    = make_invoke_for_member(spec, call_expr);
        append_format_runtime(out, templates.stateful, fmt::arg("w", spec.wrapper_name), fmt::arg("fixture", spec.callee),
                              fmt::arg("invoke", invoke));
        return;
    }
    }
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

std::string render_wrappers(const std::vector<TestCaseInfo> &cases, const WrapperTemplates &templates) {
    std::string out;
    out.reserve(cases.size() * 160);
    for (std::size_t idx = 0; idx < cases.size(); ++idx) {
        const auto &test = cases[idx];
        const auto  spec = build_wrapper_spec(test, idx);
        append_wrapper(out, spec, templates);
    }
    return out;
}

std::string render_case_entries(const std::vector<TestCaseInfo> &cases, const std::vector<std::string> &tag_names,
                                const std::vector<std::string> &req_names, const std::string &tpl_case_entry) {
    std::string out;
    out.reserve(cases.size() * 160);
    for (std::size_t idx = 0; idx < cases.size(); ++idx) {
        const auto &test             = cases[idx];
        std::string accessor_literal = "nullptr";
        const bool  has_fixture      = !test.fixture_qualified_name.empty();
        if (has_fixture) {
            const auto qualify_fixture = [&]() -> std::string {
                if (test.fixture_qualified_name.starts_with("::")) {
                    return test.fixture_qualified_name;
                }
                return std::string("::") + test.fixture_qualified_name;
            };
            switch (test.fixture_lifetime) {
            case FixtureLifetime::MemberSuite:
                accessor_literal = fmt::format("&::gentest::detail::acquire_suite_fixture<{}>", qualify_fixture());
                break;
            case FixtureLifetime::MemberGlobal:
                accessor_literal = fmt::format("&::gentest::detail::acquire_global_fixture<{}>", qualify_fixture());
                break;
            case FixtureLifetime::MemberEphemeral:
            case FixtureLifetime::None:
            default: accessor_literal = "nullptr"; break;
            }
        }

        append_format_runtime(
            out, tpl_case_entry, fmt::arg("name", escape_string(test.display_name)),
            fmt::arg("wrapper", std::string("kCaseInvoke_") + std::to_string(idx)), fmt::arg("file", escape_string(test.filename)),
            fmt::arg("line", test.line), fmt::arg("is_bench", test.is_benchmark ? "true" : "false"),
            fmt::arg("is_jitter", test.is_jitter ? "true" : "false"), fmt::arg("is_baseline", test.is_baseline ? "true" : "false"),
            fmt::arg("tags", tag_names[idx]), fmt::arg("reqs", req_names[idx]),
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

} // namespace gentest::codegen::render
