// Implementation of validation for gentest attributes

#include "validate.hpp"

#include "attr_rules.hpp"

#include <algorithm>
#include <cctype>
#include <fmt/core.h>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace gentest::codegen {

namespace {
void add_unique(std::vector<std::string> &values, std::string value) {
    if (std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(std::move(value));
    }
}

static std::string trim_copy(std::string s) {
    auto is_ws = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; };
    auto b     = s.begin();
    while (b != s.end() && is_ws(static_cast<unsigned char>(*b)))
        ++b;
    auto e = s.end();
    while (e != b && is_ws(static_cast<unsigned char>(*(e - 1))))
        --e;
    return std::string(b, e);
}
} // namespace

auto validate_attributes(const std::vector<ParsedAttribute> &parsed, const std::function<void(const std::string &)> &report)
    -> AttributeSummary {
    AttributeSummary summary;

    bool                       saw_test = false;
    bool                       saw_bench = false;
    bool                       saw_jitter = false;
    std::set<std::string>      seen_flags;
    std::optional<std::string> seen_group;
    std::optional<std::string> seen_owner;

    for (const auto &attr : parsed) {
        std::string lowered = attr.name;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

        if (lowered == "test") {
            if (saw_test) {
                summary.had_error = true;
                report("duplicate gentest attribute 'test'");
                continue;
            }
            saw_test = true;
            if (attr.arguments.size() != 1 || attr.arguments.front().empty()) {
                summary.had_error = true;
                report("'test' requires exactly one non-empty string argument");
                continue;
            }
            summary.case_name = attr.arguments.front();
        } else if (lowered == "bench" || lowered == "benchmark") {
            if (saw_bench) {
                summary.had_error = true;
                report("duplicate gentest attribute 'bench'");
                continue;
            }
            saw_bench = true;
            if (attr.arguments.size() != 1 || attr.arguments.front().empty()) {
                summary.had_error = true;
                report("'bench' requires exactly one non-empty string argument");
                continue;
            }
            summary.case_name    = attr.arguments.front();
            summary.is_benchmark = true;
        } else if (lowered == "baseline") {
            if (!attr.arguments.empty()) {
                summary.had_error = true;
                report("'baseline' does not take arguments");
                continue;
            }
            summary.is_baseline = true;
        } else if (lowered == "jitter") {
            if (saw_jitter) {
                summary.had_error = true;
                report("duplicate gentest attribute 'jitter'");
                continue;
            }
            saw_jitter = true;
            if (attr.arguments.size() != 1 || attr.arguments.front().empty()) {
                summary.had_error = true;
                report("'jitter' requires exactly one non-empty string argument");
                continue;
            }
            summary.case_name  = attr.arguments.front();
            summary.is_jitter  = true;
        } else if (lowered == "req" || lowered == "requires") {
            if (attr.arguments.empty()) {
                summary.had_error = true;
                report("'req' requires at least one string argument");
                continue;
            }
            for (const auto &req : attr.arguments) {
                add_unique(summary.requirements, req);
            }
        } else if (lowered == "skip") {
            summary.should_skip = true;
            if (!attr.arguments.empty()) {
                std::string reason = attr.arguments.front();
                for (std::size_t idx = 1; idx < attr.arguments.size(); ++idx) {
                    reason.append(", ");
                    reason.append(attr.arguments[idx]);
                }
                summary.skip_reason = std::move(reason);
            }
        } else if (lowered == "template") {
            if (attr.arguments.size() < 2) {
                summary.had_error = true;
                report("'template' requires a parameter name and at least one type");
                continue;
            }
            const std::string &raw_param = attr.arguments.front();
            std::string        param     = trim_copy(raw_param);
            if (param.empty()) {
                summary.had_error = true;
                report("'template' parameter name must be non-empty");
                continue;
            }
            bool dup = false;
            for (auto &p : summary.template_sets)
                if (p.first == param)
                    dup = true;
            if (dup) {
                summary.had_error = true;
                report("duplicate 'template' attribute for the same parameter");
                continue;
            }
            std::vector<std::string> types(attr.arguments.begin() + 1, attr.arguments.end());
            if (types.empty()) {
                summary.had_error = true;
                report("'template' requires at least one type");
                continue;
            }
            summary.template_sets.emplace_back(param, std::move(types));
        } else if (lowered == "parameters") {
            if (attr.arguments.size() < 2) {
                summary.had_error = true;
                report("'parameters' requires a parameter name and at least one value");
                continue;
            }
            AttributeSummary::ParamSet set;
            set.param_name = attr.arguments.front();
            for (std::size_t i = 1; i < attr.arguments.size(); ++i)
                set.values.push_back(attr.arguments[i]);
            summary.parameter_sets.push_back(std::move(set));
        } else if (lowered == "range") {
            // Accept forms: (name, start, step, end) or (name, "start:step:end")
            if (attr.arguments.size() < 2) {
                summary.had_error = true;
                report("'parameters_range' requires (name, start, step, end) or (name, \"start:step:end\")");
                continue;
            }
            AttributeSummary::RangeSpec spec;
            spec.name = attr.arguments.front();
            if (attr.arguments.size() == 2) {
                const std::string &expr = attr.arguments[1];
                auto a = expr.find(':');
                auto b = expr.rfind(':');
                if (a == std::string::npos || b == std::string::npos || a == b) {
                    summary.had_error = true;
                    report("'parameters_range' second argument must be of the form start:step:end");
                    continue;
                }
                spec.start = expr.substr(0, a);
                spec.step  = expr.substr(a + 1, b - a - 1);
                spec.end   = expr.substr(b + 1);
            } else if (attr.arguments.size() == 4) {
                spec.start = attr.arguments[1];
                spec.step  = attr.arguments[2];
                spec.end   = attr.arguments[3];
            } else {
                summary.had_error = true;
                report("'parameters_range' requires exactly 2 or 4 arguments");
                continue;
            }
            summary.parameter_ranges.push_back(std::move(spec));
        } else if (lowered == "linspace") {
            if (attr.arguments.size() != 4) {
                summary.had_error = true;
                report("'parameters_linspace' requires (name, start, end, count)");
                continue;
            }
            AttributeSummary::LinspaceSpec spec{attr.arguments[0], attr.arguments[1], attr.arguments[2], attr.arguments[3]};
            summary.parameter_linspaces.push_back(std::move(spec));
        } else if (lowered == "geom" || lowered == "geomspace" || lowered == "geospace") {
            if (attr.arguments.size() != 4) {
                summary.had_error = true;
                report("'geom' requires (name, start, factor, count)");
                continue;
            }
            AttributeSummary::GeomSpec spec{attr.arguments[0], attr.arguments[1], attr.arguments[2], attr.arguments[3]};
            summary.parameter_geoms.push_back(std::move(spec));
        } else if (lowered == "logspace") {
            if (attr.arguments.size() != 4 && attr.arguments.size() != 5) {
                summary.had_error = true;
                report("'logspace' requires (name, startExp, endExp, count[, base])");
                continue;
            }
            // Reuse GeomSpec storage by encoding base and exponents; expansion handled in discovery
            // We'll store as: name, startExp, endExp, count, with base optionally appended to factor field when provided.
            // Instead, add a dedicated struct in summary to avoid overloading.
            // Defer to dedicated LogspaceSpec (see validate.hpp)
            AttributeSummary::LogspaceSpec spec;
            spec.name = attr.arguments[0];
            spec.start_exp = attr.arguments[1];
            spec.end_exp = attr.arguments[2];
            spec.count = attr.arguments[3];
            if (attr.arguments.size() == 5) spec.base = attr.arguments[4];
            summary.parameter_logspaces.push_back(std::move(spec));
        } else if (lowered == "parameters_pack") {
            if (attr.arguments.size() < 2) {
                summary.had_error = true;
                report("'parameters_pack' requires a parameter name tuple and at least one value tuple");
                continue;
            }
            auto parse_tuple = [&](const std::string &text, std::vector<std::string> &out) {
                std::string s = text;
                // Strip outer parens if present
                if (!s.empty() && s.front() == '(' && s.back() == ')') {
                    s = s.substr(1, s.size() - 2);
                }
                // Split on commas with nesting support by reusing split_arguments-like logic
                // Here we implement a small local splitter: no quotes unescaping required.
                std::vector<std::string> parts;
                std::string              cur;
                int                      depth  = 0;
                bool                     in_str = false;
                bool                     esc    = false;
                for (char ch : s) {
                    if (in_str) {
                        cur.push_back(ch);
                        if (esc)
                            esc = false;
                        else if (ch == '\\')
                            esc = true;
                        else if (ch == '"')
                            in_str = false;
                        continue;
                    }
                    if (ch == '"') {
                        in_str = true;
                        cur.push_back(ch);
                        continue;
                    }
                    if (ch == '(' || ch == '[' || ch == '{') {
                        ++depth;
                        cur.push_back(ch);
                        continue;
                    }
                    if (ch == ')' || ch == ']' || ch == '}') {
                        if (depth > 0)
                            --depth;
                        cur.push_back(ch);
                        continue;
                    }
                    if (ch == ',' && depth == 0) {
                        if (!cur.empty()) {
                            std::string t = cur; // trim
                            auto        l = t.find_first_not_of(" \t\n\r");
                            auto        r = t.find_last_not_of(" \t\n\r");
                            if (l != std::string::npos)
                                out.push_back(t.substr(l, r - l + 1));
                        }
                        cur.clear();
                        continue;
                    }
                    cur.push_back(ch);
                }
                if (!cur.empty()) {
                    std::string t = cur;
                    auto        l = t.find_first_not_of(" \t\n\r");
                    auto        r = t.find_last_not_of(" \t\n\r");
                    if (l != std::string::npos)
                        out.push_back(t.substr(l, r - l + 1));
                }
            };
            AttributeSummary::ParamPack pack;
            // First argument: parameter names tuple
            parse_tuple(attr.arguments.front(), pack.names);
            if (pack.names.empty()) {
                summary.had_error = true;
                report("'parameters_pack' first tuple must list at least one parameter name");
                continue;
            }
            // Remaining arguments: value tuples matching arity
            for (std::size_t i = 1; i < attr.arguments.size(); ++i) {
                std::vector<std::string> row;
                parse_tuple(attr.arguments[i], row);
                if (row.size() != pack.names.size()) {
                    summary.had_error = true;
                    report("'parameters_pack' value tuple arity mismatch");
                    continue;
                }
                pack.rows.push_back(std::move(row));
            }
            if (pack.rows.empty()) {
                summary.had_error = true;
                report("'parameters_pack' requires at least one value tuple");
                continue;
            }
            summary.param_packs.push_back(std::move(pack));
        } else if (lowered == "fixtures") {
            if (attr.arguments.empty()) {
                summary.had_error = true;
                report("'fixtures' requires at least one type name");
                continue;
            }
            for (const auto &ty : attr.arguments) {
                if (ty.empty()) {
                    summary.had_error = true;
                    report("'fixtures' contains an empty type token");
                    break;
                }
                summary.fixtures_types.push_back(ty);
            }
        } else if (attr.arguments.empty()) {
            if (!gentest::detail::is_allowed_flag_attribute(lowered)) {
                summary.had_error = true;
                report(fmt::format("unknown gentest attribute '{}'", attr.name));
                continue;
            }
            if (seen_flags.contains(lowered)) {
                summary.had_error = true;
                report(fmt::format("duplicate gentest flag attribute '{}'", attr.name));
                continue;
            }
            if ((lowered == "linux" && seen_flags.contains("windows")) || (lowered == "windows" && seen_flags.contains("linux"))) {
                summary.had_error = true;
                report("conflicting gentest flags 'linux' and 'windows'");
                continue;
            }
            seen_flags.insert(lowered);
            add_unique(summary.tags, attr.name);
        } else {
            if (!gentest::detail::is_allowed_value_attribute(lowered)) {
                summary.had_error = true;
                std::string joined;
                for (std::size_t idx = 0; idx < attr.arguments.size(); ++idx) {
                    if (idx != 0)
                        joined += ", ";
                    joined += '"';
                    joined += attr.arguments[idx];
                    joined += '"';
                }
                report(fmt::format("unknown gentest attribute '{}' with argument{} ({})", attr.name, attr.arguments.size() == 1 ? "" : "s",
                                   joined));
                continue;
            }
            if (lowered == "group") {
                if (attr.arguments.size() != 1) {
                    summary.had_error = true;
                    report(fmt::format("'{}' requires exactly one string argument", lowered));
                    continue;
                }
                if (seen_group.has_value()) {
                    summary.had_error = true;
                    report(fmt::format("duplicate '{}' attribute", lowered));
                    continue;
                }
                seen_group = attr.arguments.front();
                add_unique(summary.tags, attr.name + "=" + attr.arguments.front());
            } else if (lowered == "owner") {
                if (attr.arguments.size() != 1) {
                    summary.had_error = true;
                    report(fmt::format("'{}' requires exactly one string argument", lowered));
                    continue;
                }
                if (seen_owner.has_value()) {
                    summary.had_error = true;
                    report(fmt::format("duplicate '{}' attribute", lowered));
                    continue;
                }
                seen_owner = attr.arguments.front();
                add_unique(summary.tags, attr.name + "=" + attr.arguments.front());
            }
        }
    }

    // 'test("...")' is optional: if absent, we fall back to the C++ function name.

    return summary;
}

auto validate_fixture_attributes(const std::vector<ParsedAttribute> &parsed, const std::function<void(const std::string &)> &report)
    -> FixtureAttributeSummary {
    FixtureAttributeSummary summary{};
    bool                    saw_fixture = false;

    for (const auto &attr : parsed) {
        std::string lowered = attr.name;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

        if (gentest::detail::is_allowed_fixture_attribute(lowered)) {
            if (saw_fixture) {
                summary.had_error = true;
                report("duplicate gentest attribute 'fixture' on fixture type");
                continue;
            }
            saw_fixture = true;
            if (attr.arguments.size() != 1) {
                summary.had_error = true;
                report("'fixture' requires exactly one argument: 'suite' or 'global'");
                continue;
            }
            std::string arg        = attr.arguments.front();
            std::string normalized = arg;
            std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if (normalized == "suite") {
                summary.lifetime = FixtureLifetime::MemberSuite;
            } else if (normalized == "global") {
                summary.lifetime = FixtureLifetime::MemberGlobal;
            } else {
                summary.had_error = true;
                report(fmt::format("unknown fixture scope '{}'; expected 'suite' or 'global'", arg));
            }
            continue;
        }

        // All other gentest attributes are unknown at class scope.
        summary.had_error = true;
        std::string joined;
        if (!attr.arguments.empty()) {
            for (std::size_t idx = 0; idx < attr.arguments.size(); ++idx) {
                if (idx != 0)
                    joined += ", ";
                joined += '"';
                joined += attr.arguments[idx];
                joined += '"';
            }
            report(fmt::format("unknown gentest class attribute '{}' with argument{} ({})", attr.name,
                               attr.arguments.size() == 1 ? "" : "s", joined));
        } else {
            report(fmt::format("unknown gentest class attribute '{}'", attr.name));
        }
    }

    return summary;
}

auto validate_namespace_attributes(const std::vector<ParsedAttribute> &parsed, const std::function<void(const std::string &)> &report)
    -> SuiteAttributeSummary {
    SuiteAttributeSummary summary{};
    bool                  saw_suite = false;

    for (const auto &attr : parsed) {
        std::string lowered = attr.name;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

        if (lowered == "suite") {
            if (saw_suite) {
                summary.had_error = true;
                report("duplicate gentest namespace attribute 'suite'");
                continue;
            }
            if (attr.arguments.size() != 1) {
                summary.had_error = true;
                report("'suite' requires exactly one string argument");
                continue;
            }
            if (attr.arguments.front().empty()) {
                summary.had_error = true;
                report("'suite' argument must not be empty");
                continue;
            }
            saw_suite          = true;
            summary.suite_name = attr.arguments.front();
            continue;
        }

        summary.had_error = true;
        std::string joined;
        if (!attr.arguments.empty()) {
            for (std::size_t idx = 0; idx < attr.arguments.size(); ++idx) {
                if (idx != 0)
                    joined += ", ";
                joined += '"';
                joined += attr.arguments[idx];
                joined += '"';
            }
            report(fmt::format("unknown gentest namespace attribute '{}' with argument{} ({})", attr.name,
                               attr.arguments.size() == 1 ? "" : "s", joined));
        } else {
            report(fmt::format("unknown gentest namespace attribute '{}'", attr.name));
        }
    }

    return summary;
}

} // namespace gentest::codegen
