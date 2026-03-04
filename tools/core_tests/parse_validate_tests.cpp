#include "parse_core.hpp"
#include "validate.hpp"

#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using gentest::codegen::parse_attribute_list;
using gentest::codegen::ParsedAttribute;
using gentest::codegen::validate_attributes;

struct Run {
    int  failures = 0;
    void expect(bool ok, std::string_view msg) {
        if (!ok) {
            ++failures;
            std::cerr << "FAIL: " << msg << "\n";
        }
    }
};

int main() {
    Run t;

    {
        auto                     attrs = parse_attribute_list(R"(test("suite/a"), slow, linux, req("#1"), owner("bar"))");
        std::vector<std::string> diags;
        auto                     summary = validate_attributes(attrs, [&](const std::string &m) { diags.push_back(m); });
        t.expect(diags.empty(), "valid attributes produce no diagnostics");
        t.expect(summary.had_error == false, "valid attributes should not error");
        t.expect(summary.case_name && *summary.case_name == "suite/a", "test name parsed");
        auto has = [&](std::string_view needle) {
            for (const auto &s : summary.tags)
                if (s == needle)
                    return true;
            return false;
        };
        t.expect(has("slow"), "flag 'slow' present");
        t.expect(has("linux"), "flag 'linux' present");
        t.expect(has("owner=bar"), "owner value present");
        t.expect(summary.requirements.size() == 1 && summary.requirements[0] == "#1", "single req present");
    }

    {
        auto attrs = parse_attribute_list(R"(gentest::test("suite/scoped"), gentest::slow, gentest::req("#2"), gentest::owner("ops"))");
        std::vector<std::string> diags;
        auto                     summary = validate_attributes(attrs, [&](const std::string &m) { diags.push_back(m); });
        t.expect(diags.empty(), "scoped gentest:: attributes produce no diagnostics");
        t.expect(summary.had_error == false, "scoped gentest:: attributes should not error");
        t.expect(summary.case_name && *summary.case_name == "suite/scoped", "scoped test name parsed");
        auto has = [&](std::string_view needle) {
            for (const auto &s : summary.tags)
                if (s == needle)
                    return true;
            return false;
        };
        t.expect(has("slow"), "scoped flag is normalized");
        t.expect(has("owner=ops"), "scoped owner is normalized");
        t.expect(summary.requirements.size() == 1 && summary.requirements[0] == "#2", "scoped req present");
    }

    {
        auto                     attrs = parse_attribute_list(R"(test("suite/other"), other::slow)");
        std::vector<std::string> diags;
        auto                     summary = validate_attributes(attrs, [&](const std::string &m) { diags.push_back(m); });
        t.expect(summary.had_error, "non-gentest scoped attribute errors");
        t.expect(!diags.empty(), "non-gentest scoped attribute reports a diagnostic");
    }

    // Range/linspace/geom/logspace parse smoke
    {
        auto attrs =
            parse_attribute_list(R"(test("x"), range(i, 1, 2, 9), linspace(x, 0.0, 1.0, 5), geom(n, 1, 2, 4), logspace(f, -3, 3, 7))");
        std::vector<std::string> diags;
        auto                     summary = validate_attributes(attrs, [&](const std::string &m) { diags.push_back(m); });
        t.expect(!summary.had_error, "param generators parse without error");
    }

    {
        auto                     attrs = parse_attribute_list(R"(test("x"), test("y"))");
        std::vector<std::string> diags;
        auto                     summary = validate_attributes(attrs, [&](const std::string &m) { diags.push_back(m); });
        t.expect(summary.had_error, "duplicate 'test' errors");
        t.expect(!diags.empty(), "duplicate 'test' reports a diagnostic");
    }

    {
        auto                     attrs = parse_attribute_list(R"(linux, windows, test("x"))");
        std::vector<std::string> diags;
        auto                     summary = validate_attributes(attrs, [&](const std::string &m) { diags.push_back(m); });
        t.expect(summary.had_error, "conflicting platform flags error");
        t.expect(!diags.empty(), "conflicting flags report a diagnostic");
    }

    {
        auto                     attrs = parse_attribute_list(R"(test("x"), req())");
        std::vector<std::string> diags;
        auto                     summary = validate_attributes(attrs, [&](const std::string &m) { diags.push_back(m); });
        t.expect(summary.had_error, "req without arguments errors");
    }

    {
        auto                     attrs = parse_attribute_list(R"(test("x"), owner("a"), owner("b"))");
        std::vector<std::string> diags;
        auto                     summary = validate_attributes(attrs, [&](const std::string &m) { diags.push_back(m); });
        t.expect(summary.had_error, "duplicate owner errors");
    }

    {
        auto                     attrs = parse_attribute_list(R"(test("x"), gpu)");
        std::vector<std::string> diags;
        auto                     summary = validate_attributes(attrs, [&](const std::string &m) { diags.push_back(m); });
        t.expect(summary.had_error, "unknown gentest flag errors");
    }

    {
        auto                     attrs = parse_attribute_list(R"(test("x"), not_allowed("value"))");
        std::vector<std::string> diags;
        auto                     summary = validate_attributes(attrs, [&](const std::string &m) { diags.push_back(m); });
        t.expect(summary.had_error, "unknown gentest value attribute errors");
        t.expect(!diags.empty(), "unknown value attribute reports a diagnostic");
    }

    // Regression: split_arguments must treat '<...>' as a nesting delimiter so template commas
    // do not split a single argument into multiple pieces.
    {
        auto attrs = parse_attribute_list(R"(template(T, std::pair<int, int>), test("templated"))");
        t.expect(attrs.size() == 2, "template regression: expected two attributes");
        t.expect(attrs[0].name == "template", "template regression: first attribute name");
        t.expect(attrs[0].arguments.size() == 2, "template regression: template argument count");
        if (attrs[0].arguments.size() >= 2) {
            t.expect(attrs[0].arguments[0] == "T", "template regression: parameter name preserved");
            t.expect(attrs[0].arguments[1] == "std::pair<int, int>", "template regression: template type preserved");
        }
    }

    // Regression: parameters_pack tuple splitting must also keep '<...>' intact.
    {
        auto attrs = parse_attribute_list(
            R"(test("x"), parameters_pack((value), (std::pair<int, int>{1, 2}), (std::pair<int, int>{3, 4})))");
        std::vector<std::string> diags;
        auto                     summary = validate_attributes(attrs, [&](const std::string &m) { diags.push_back(m); });
        t.expect(!summary.had_error, "parameters_pack regression: template commas must not trigger arity mismatch");
        t.expect(!summary.param_packs.empty(), "parameters_pack regression: pack parsed");
        if (!summary.param_packs.empty()) {
            t.expect(summary.param_packs[0].names.size() == 1, "parameters_pack regression: one parameter name");
            t.expect(summary.param_packs[0].rows.size() == 2, "parameters_pack regression: two rows");
            if (summary.param_packs[0].rows.size() >= 2) {
                t.expect(summary.param_packs[0].rows[0].size() == 1, "parameters_pack regression: row0 arity");
                t.expect(summary.param_packs[0].rows[1].size() == 1, "parameters_pack regression: row1 arity");
            }
        }
    }

    if (t.failures) {
        std::cerr << "Total failures: " << t.failures << "\n";
        return 1;
    }
    return 0;
}
