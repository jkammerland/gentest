// Implementation of validation for gentest attributes

#include "validate.hpp"
#include "attr_rules.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <set>
#include <fmt/core.h>
#include <string>
#include <vector>

namespace gentest::codegen {

namespace {
void add_unique(std::vector<std::string> &values, std::string value) {
    if (std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(std::move(value));
    }
}
} // namespace

auto validate_attributes(const std::vector<ParsedAttribute> &parsed,
                         const std::function<void(const std::string &)> &report) -> AttributeSummary {
    AttributeSummary summary;

    bool                        saw_test      = false;
    std::set<std::string>       seen_flags;
    std::optional<std::string>  seen_category;
    std::optional<std::string>  seen_owner;

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
            const std::string &param = attr.arguments.front();
            if (param.empty()) {
                summary.had_error = true;
                report("'template' parameter name must be non-empty");
                continue;
            }
            bool dup = false;
            for (auto &p : summary.template_sets) if (p.first == param) dup = true;
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
                report("'parameters' requires a type and at least one value");
                continue;
            }
            AttributeSummary::ParamSet set;
            set.type_name = attr.arguments.front();
            for (std::size_t i = 1; i < attr.arguments.size(); ++i) set.values.push_back(attr.arguments[i]);
            summary.parameter_sets.push_back(std::move(set));
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
                    if (idx != 0) joined += ", ";
                    joined += '"';
                    joined += attr.arguments[idx];
                    joined += '"';
                }
                report(fmt::format("unknown gentest attribute '{}' with argument{} ({})",
                                   attr.name, attr.arguments.size() == 1 ? "" : "s", joined));
                continue;
            }
            if (lowered == "category") {
                if (attr.arguments.size() != 1) {
                summary.had_error = true;
                report(fmt::format("'{}' requires exactly one string argument", lowered));
                continue;
            }
                if (seen_category.has_value()) {
                summary.had_error = true;
                report(fmt::format("duplicate '{}' attribute", lowered));
                continue;
            }
                seen_category = attr.arguments.front();
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

    if (!summary.case_name.has_value()) {
        summary.had_error = true;
        report("expected [[using gentest : test(\"...\")]] attribute on this test function");
    }

    return summary;
}

auto validate_fixture_attributes(const std::vector<ParsedAttribute> &parsed,
                                 const std::function<void(const std::string &)> &report) -> FixtureAttributeSummary {
    FixtureAttributeSummary summary{};
    bool                    saw_stateful = false;

    for (const auto &attr : parsed) {
        std::string lowered = attr.name;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

        if (attr.arguments.empty()) {
            if (gentest::detail::is_allowed_fixture_flag(lowered)) {
                if (lowered == "stateful_fixture") {
                    if (saw_stateful) {
                        summary.had_error = true;
                        report("duplicate gentest attribute 'stateful_fixture' on fixture");
                        continue;
                    }
                    saw_stateful     = true;
                    summary.stateful = true;
                }
                continue;
            }
        }

        // All other gentest attributes are unknown at class scope.
        summary.had_error = true;
        std::string joined;
        if (!attr.arguments.empty()) {
            for (std::size_t idx = 0; idx < attr.arguments.size(); ++idx) {
                if (idx != 0) joined += ", ";
                joined += '"';
                joined += attr.arguments[idx];
                joined += '"';
            }
            report(fmt::format("unknown gentest class attribute '{}' with argument{} ({})",
                               attr.name, attr.arguments.size() == 1 ? "" : "s", joined));
        } else {
            report(fmt::format("unknown gentest class attribute '{}'", attr.name));
        }
    }

    return summary;
}

} // namespace gentest::codegen
