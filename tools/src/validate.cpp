// Implementation of validation for gentest attributes

#include "validate.hpp"

#include "attr_rules.hpp"
#include "gentest/detail/utf8.h"
#include "parse_core.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fmt/format.h>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace gentest::codegen {

namespace {
void add_unique(std::vector<std::string> &values, std::string value) {
    if (std::ranges::find(values, value) == values.end()) {
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
    return {b, e};
}

bool parse_positive_u64(std::string_view text, std::uint64_t &out) {
    if (text.empty()) {
        return false;
    }
    if (text.size() > 1 && text.front() == '0') {
        return false;
    }
    std::uint64_t value = 0;
    for (const char ch : text) {
        if (ch < '0' || ch > '9') {
            return false;
        }
        const auto digit = static_cast<std::uint64_t>(ch - '0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) {
            return false;
        }
        value = value * 10 + digit;
    }
    if (value == 0) {
        return false;
    }
    out = value;
    return true;
}

// Require a single quoted literal, not an expression that merely
// begins and ends in quotes (for example "a" + "b").
bool isStringLiteral(std::string_view token) {
    if (token.size() < 2 || token.front() != '"' || token.back() != '"')
        return false;
    for (std::size_t j = 1; j + 1 < token.size(); ++j) {
        if (token[j] == '\\') {
            ++j;
            if (j + 1 >= token.size())
                return false;
        } else if (token[j] == '"')
            return false;
    }
    return true;
}

bool isValidRowId(std::string_view id) {
    if (id.find_first_not_of(' ') == std::string_view::npos)
        return false;

    for (std::size_t pos = 0; pos < id.size();) {
        const auto [code_point, width, valid] = gentest::detail::decodeUtf8(id, pos);
        // Keep names representable in CLI arguments, JSON and JUnit XML.
        if (!valid || code_point < 0x20 || (code_point >= 0x7F && code_point < 0xA0) || code_point == 0xFFFE || code_point == 0xFFFF)
            return false;
        pos += width;
    }
    return true;
}

} // namespace

auto validate_attributes(const std::vector<ParsedAttribute> &parsed, const std::function<void(const std::string &)> &report)
    -> AttributeSummary {
    AttributeSummary summary;

    bool                       saw_case           = false;
    bool                       saw_test           = false;
    bool                       saw_bench          = false;
    bool                       saw_jitter         = false;
    bool                       saw_items_per_call = false;
    bool                       saw_bench_once     = false;
    bool                       saw_bench_warmup   = false;
    bool                       saw_bench_time     = false;
    std::set<std::string>      seen_flags;
    std::optional<std::string> seen_owner;

    for (const auto &attr : parsed) {
        std::string lowered = attr.name;
        std::ranges::transform(lowered, lowered.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

        if (lowered == "test") {
            if (saw_test) {
                summary.had_error = true;
                report("duplicate gentest attribute 'test'");
                continue;
            }
            saw_test = true;
            saw_case = true;
            if (attr.arguments.empty()) {
                continue;
            }
            if (attr.arguments.size() != 1 || trim_copy(attr.arguments.front()).empty()) {
                summary.had_error = true;
                report("'test' takes zero arguments (default name) or exactly one non-empty string argument");
                continue;
            }
            summary.case_name = trim_copy(attr.arguments.front());
        } else if (lowered == "bench" || lowered == "benchmark") {
            if (saw_bench) {
                summary.had_error = true;
                report("duplicate gentest attribute 'bench'");
                continue;
            }
            saw_bench = true;
            saw_case  = true;
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
            saw_case   = true;
            if (attr.arguments.size() != 1 || attr.arguments.front().empty()) {
                summary.had_error = true;
                report("'jitter' requires exactly one non-empty string argument");
                continue;
            }
            summary.case_name = attr.arguments.front();
            summary.is_jitter = true;
        } else if (lowered == "items_per_call" || lowered == "ops_per_call") {
            if (saw_items_per_call) {
                summary.had_error = true;
                report("duplicate measured item-count attribute");
                continue;
            }
            saw_items_per_call = true;
            if (attr.arguments.size() != 1) {
                summary.had_error = true;
                report(fmt::format("'{}' requires exactly one positive integer argument", lowered));
                continue;
            }
            std::uint64_t items_per_call = 1;
            if (!parse_positive_u64(trim_copy(attr.arguments.front()), items_per_call)) {
                summary.had_error = true;
                report(fmt::format("'{}' requires exactly one positive integer argument", lowered));
                continue;
            }
            summary.items_per_call = items_per_call;
        } else if (lowered == "bench_once") {
            if (saw_bench_once) {
                summary.had_error = true;
                report("duplicate gentest attribute 'bench_once'");
                continue;
            }
            saw_bench_once = true;
            if (!attr.arguments.empty()) {
                summary.had_error = true;
                report("'bench_once' does not take arguments");
                continue;
            }
            summary.benchmark.once = true;
        } else if (lowered == "bench_warmup") {
            if (saw_bench_warmup) {
                summary.had_error = true;
                report("duplicate gentest attribute 'bench_warmup'");
                continue;
            }
            saw_bench_warmup     = true;
            std::uint64_t warmup = 0;
            const auto   &tokens = attr.argument_tokens.empty() ? attr.arguments : attr.argument_tokens;
            if (tokens.size() != 1 || (trim_copy(tokens.front()) != "0" && !parse_positive_u64(trim_copy(tokens.front()), warmup))) {
                summary.had_error = true;
                report("'bench_warmup' requires exactly one non-negative canonical decimal integer argument");
                continue;
            }
            summary.benchmark.warmup_epochs = warmup;
        } else if (lowered == "bench_time") {
            if (saw_bench_time) {
                summary.had_error = true;
                report("duplicate gentest attribute 'bench_time'");
                continue;
            }
            saw_bench_time     = true;
            const auto &tokens = attr.argument_tokens.empty() ? attr.arguments : attr.argument_tokens;
            if (tokens.size() != 1 || (tokens.front() != "\"runner\"" && tokens.front() != "\"supplied\"")) {
                summary.had_error = true;
                report("'bench_time' requires exactly one string argument: 'runner' or 'supplied'");
                continue;
            }
            summary.benchmark.supplied_time = attr.arguments.front() == "supplied";
        } else if (lowered == "req" || lowered == "requires") {
            if (attr.arguments.empty()) {
                summary.had_error = true;
                report("'req' requires at least one string argument");
                continue;
            }
            saw_case = true;
            for (const auto &req : attr.arguments) {
                add_unique(summary.requirements, req);
            }
        } else if (lowered == "skip") {
            saw_case            = true;
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
                report("'template' requires a parameter name and at least one binding");
                continue;
            }
            saw_case                     = true;
            const std::string &raw_param = attr.arguments.front();
            std::string        param     = trim_copy(raw_param);
            if (param.empty()) {
                summary.had_error = true;
                report("'template' parameter name must be non-empty");
                continue;
            }
            bool dup = false;
            for (auto &p : summary.template_sets)
                if (p.param_name == param)
                    dup = true;
            if (dup) {
                summary.had_error = true;
                report("duplicate 'template' attribute for the same parameter");
                continue;
            }
            std::vector<std::string> bindings(attr.arguments.begin() + 1, attr.arguments.end());
            if (bindings.empty()) {
                summary.had_error = true;
                report("'template' requires at least one binding");
                continue;
            }
            summary.template_sets.push_back(TemplateBindingSet{
                .param_name = param,
                .candidates = std::move(bindings),
            });
        } else if (lowered == "parameters") {
            if (attr.arguments.size() < 2) {
                summary.had_error = true;
                report("'parameters' requires a parameter name and at least one value");
                continue;
            }
            saw_case = true;
            AttributeSummary::ParamSet set;
            set.param_name = attr.arguments.front();
            // Keep the spelling of string literals: the generated call passes
            // them through verbatim, so every escape sequence survives.
            for (std::size_t i = 1; i < attr.arguments.size(); ++i) {
                const bool literal_token = i < attr.argument_tokens.size() && attr.argument_tokens[i].size() >= 2 &&
                                           attr.argument_tokens[i].front() == '"' && attr.argument_tokens[i].back() == '"';
                set.values.push_back(literal_token ? attr.argument_tokens[i] : attr.arguments[i]);
            }
            summary.parameter_sets.push_back(std::move(set));
        } else if (lowered == "range") {
            // Accept forms: (name, start, step, end) or (name, "start:step:end")
            if (attr.arguments.size() < 2) {
                summary.had_error = true;
                report("'parameters_range' requires (name, start, step, end) or (name, \"start:step:end\")");
                continue;
            }
            saw_case = true;
            AttributeSummary::RangeSpec spec;
            spec.name = attr.arguments.front();
            if (attr.arguments.size() == 2) {
                const std::string &expr = attr.arguments[1];
                auto               a    = expr.find(':');
                auto               b    = expr.rfind(':');
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
            saw_case = true;
            AttributeSummary::LinspaceSpec spec{
                .name  = attr.arguments[0],
                .start = attr.arguments[1],
                .end   = attr.arguments[2],
                .count = attr.arguments[3],
            };
            summary.parameter_linspaces.push_back(std::move(spec));
        } else if (lowered == "geom" || lowered == "geomspace" || lowered == "geospace") {
            if (attr.arguments.size() != 4) {
                summary.had_error = true;
                report("'geom' requires (name, start, factor, count)");
                continue;
            }
            saw_case = true;
            AttributeSummary::GeomSpec spec{
                .name   = attr.arguments[0],
                .start  = attr.arguments[1],
                .factor = attr.arguments[2],
                .count  = attr.arguments[3],
            };
            summary.parameter_geoms.push_back(std::move(spec));
        } else if (lowered == "logspace") {
            if (attr.arguments.size() != 4 && attr.arguments.size() != 5) {
                summary.had_error = true;
                report("'logspace' requires (name, startExp, endExp, count[, base])");
                continue;
            }
            saw_case = true;
            // Reuse GeomSpec storage by encoding base and exponents; expansion handled in discovery
            // We'll store as: name, startExp, endExp, count, with base optionally appended to factor field when provided.
            // Instead, add a dedicated struct in summary to avoid overloading.
            // Defer to dedicated LogspaceSpec (see validate.hpp)
            AttributeSummary::LogspaceSpec spec;
            spec.name      = attr.arguments[0];
            spec.start_exp = attr.arguments[1];
            spec.end_exp   = attr.arguments[2];
            spec.count     = attr.arguments[3];
            if (attr.arguments.size() == 5)
                spec.base = attr.arguments[4];
            summary.parameter_logspaces.push_back(std::move(spec));
        } else if (lowered == "parameters_pack") {
            if (attr.arguments.size() < 2) {
                summary.had_error = true;
                report("'parameters_pack' requires a parameter name tuple and at least one value tuple");
                continue;
            }
            saw_case         = true;
            auto parse_tuple = [](std::string_view text) {
                if (text.size() >= 2 && text.front() == '(' && text.back() == ')') {
                    text.remove_prefix(1);
                    text.remove_suffix(1);
                }
                return split_expression_list(text);
            };
            AttributeSummary::ParamPack pack;
            // First argument: parameter names tuple
            pack.names = parse_tuple(attr.arguments.front());
            if (pack.names.empty()) {
                summary.had_error = true;
                report("'parameters_pack' first tuple must list at least one parameter name");
                continue;
            }
            // Each row can start with an ID in addition to its parameter values.
            std::set<std::string> row_ids;
            for (std::size_t i = 1; i < attr.arguments.size(); ++i) {
                auto        row = parse_tuple(attr.arguments[i]);
                std::string id;
                if (row.size() == pack.names.size() + 1 && row.front().starts_with('"')) {
                    auto valid_literal = isStringLiteral(row.front());
                    if (valid_literal)
                        id = decode_string_literal(row.front(), &valid_literal);
                    if (!valid_literal || !isValidRowId(id)) {
                        summary.had_error = true;
                        report("'parameters_pack' row ID must be a nonblank UTF-8 string literal with valid escapes and no control "
                               "characters");
                        continue;
                    }
                    if (!row_ids.insert(id).second) {
                        summary.had_error = true;
                        report(fmt::format("'parameters_pack' duplicate row ID '{}'", id));
                        continue;
                    }
                    row.erase(row.begin());
                } else if (row.size() != pack.names.size()) {
                    summary.had_error = true;
                    report("'parameters_pack' value tuple arity mismatch");
                    continue;
                }
                if (!pack.rows.empty() && (pack.rows.front().id.empty() != id.empty())) {
                    summary.had_error = true;
                    report("'parameters_pack' requires an ID on every row in a named pack");
                    continue;
                }
                pack.rows.push_back({.id = std::move(id), .values = std::move(row)});
            }
            if (pack.rows.empty()) {
                summary.had_error = true;
                report("'parameters_pack' requires at least one value tuple");
                continue;
            }
            summary.param_packs.push_back(std::move(pack));
        } else if (lowered == "tag") {
            saw_case = true;
            if (attr.arguments.empty()) {
                summary.had_error = true;
                report("'tag' requires at least one nonempty string literal");
            }
            for (std::size_t i = 0; i < attr.arguments.size(); ++i) {
                auto value         = attr.arguments[i];
                bool valid_literal = i < attr.argument_tokens.size() && isStringLiteral(attr.argument_tokens[i]);
                // Unknown escapes must not silently become different user labels.
                if (valid_literal)
                    value = decode_string_literal(attr.argument_tokens[i], &valid_literal);
                if (i < attr.argument_tokens.size() && attr.argument_tokens[i].starts_with("R\"")) {
                    const std::string_view token = attr.argument_tokens[i];
                    const auto             open  = token.find('(', 2);
                    if (open != std::string_view::npos && open - 2 <= 16) {
                        const auto        delimiter = token.substr(2, open - 2);
                        const std::string closing   = ")" + std::string(delimiter) + "\"";
                        const auto        end       = token.find(closing, open + 1);
                        if (delimiter.find_first_of(" \\)\t\n\r") == std::string_view::npos && end != std::string_view::npos &&
                            end + closing.size() == token.size()) {
                            value         = token.substr(open + 1, end - open - 1);
                            valid_literal = true;
                        }
                    }
                }
                if (!valid_literal || value.empty() || value.find('\0') != std::string::npos) {
                    summary.had_error = true;
                    report("'tag' requires nonempty ordinary or raw string literals; use C++20 escapes and no NUL characters");
                    continue;
                }
                // Selection compares ASCII case-insensitively; keep the first spelling.
                auto normalize = [](std::string text) {
                    for (char &ch : text)
                        if (ch >= 'A' && ch <= 'Z')
                            ch = static_cast<char>(ch + ('a' - 'A'));
                    return text;
                };
                if (std::ranges::none_of(summary.tags, [&](const auto &tag) { return normalize(tag) == normalize(value); }))
                    summary.tags.push_back(value);
            }
        } else if (lowered == "fast" || lowered == "slow" || lowered == "linux" || lowered == "windows") {
            summary.had_error = true;
            report(fmt::format("gentest attribute '{}' was removed; use tag(\"{}\") instead", lowered, lowered));
        } else if (lowered == "fixtures") {
            summary.had_error = true;
            report("'fixtures(...)' was removed; infer fixtures from function parameters instead");
            continue;
        } else if (attr.arguments.empty()) {
            if (!gentest::detail::is_allowed_flag_attribute(lowered)) {
                summary.had_error = true;
                report(fmt::format("unknown gentest attribute '{}'", attr.name));
                continue;
            }
            saw_case = true;
            if (seen_flags.contains(lowered)) {
                summary.had_error = true;
                report(fmt::format("duplicate gentest flag attribute '{}'", attr.name));
                continue;
            }
            seen_flags.insert(lowered);
            summary.is_death = true;
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
            if (lowered == "owner") {
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
                saw_case      = true;
                seen_owner    = attr.arguments.front();
                summary.owner = attr.arguments.front();
            }
        }
    }

    // baseline is only meaningful for benchmarks/jitter measurements.
    if (summary.is_baseline && !summary.is_benchmark && !summary.is_jitter) {
        summary.had_error = true;
        report("'baseline' requires 'bench' or 'jitter' on the same declaration");
    }
    if (saw_items_per_call && !summary.is_benchmark && !summary.is_jitter) {
        summary.had_error = true;
        report("'items_per_call'/'ops_per_call' requires 'bench' or 'jitter' on the same declaration");
    }

    // These policies describe benchmark execution, never ordinary tests or jitter sampling.
    if ((saw_bench_once || saw_bench_warmup || saw_bench_time) && (!summary.is_benchmark || saw_test || summary.is_jitter)) {
        summary.had_error = true;
        report("'bench_once', 'bench_warmup', and 'bench_time' require 'bench' without 'test' or 'jitter' on the same declaration");
    }
    if (summary.benchmark.supplied_time && !summary.benchmark.once) {
        summary.had_error = true;
        report("'bench_time(\"supplied\")' requires 'bench_once'");
    }
    if (summary.benchmark.once && summary.benchmark.warmup_epochs.value_or(0) != 0) {
        summary.had_error = true;
        report("'bench_once' cannot be combined with positive 'bench_warmup'");
    }

    summary.is_case = saw_case;

    return summary;
}

auto validate_fixture_attributes(const std::vector<ParsedAttribute> &parsed, const std::function<void(const std::string &)> &report)
    -> FixtureAttributeSummary {
    FixtureAttributeSummary summary{};
    bool                    saw_fixture = false;

    for (const auto &attr : parsed) {
        std::string lowered = attr.name;
        std::ranges::transform(lowered, lowered.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

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
            std::ranges::transform(normalized, normalized.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
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
    -> NamespaceAttributeSummary {
    NamespaceAttributeSummary summary{};
    bool                      reported_suite = false;

    for (const auto &attr : parsed) {
        std::string lowered = attr.name;
        std::ranges::transform(lowered, lowered.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

        summary.had_error = true;
        if (lowered == "suite") {
            // Any spelling or argument list gets the same error, reported once per list.
            if (!reported_suite) {
                reported_suite = true;
                report("namespace attribute 'suite' was removed in gentest 2.0.0; cases take their suite and name prefix from the "
                       "namespace path. Name cases with test(\"...\") or split them into separate test executables instead");
            }
            continue;
        }

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
