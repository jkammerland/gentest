#include "parse_core.hpp"
#include "validate.hpp"

#include <algorithm>
#include <cctype>
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

auto normalize_ws(std::string value) -> std::string {
    const auto removed = std::ranges::remove_if(value, [](unsigned char c) { return std::isspace(c) != 0; });
    value.erase(removed.begin(), removed.end());
    return value;
}

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
        if (!attrs.empty()) {
            t.expect(attrs[0].name == "template", "template regression: first attribute name");
            t.expect(attrs[0].arguments.size() == 2, "template regression: template argument count");
            if (attrs[0].arguments.size() >= 2) {
                t.expect(attrs[0].arguments[0] == "T", "template regression: parameter name preserved");
                t.expect(normalize_ws(attrs[0].arguments[1]) == "std::pair<int,int>", "template regression: template type preserved");
            }
        }
    }

    // Regression: spaced template argument lists should be preserved as a single argument.
    {
        auto attrs = parse_attribute_list(R"(template(T, std::pair <int, int>), test("templated-space"))");
        t.expect(attrs.size() == 2, "template spacing regression: expected two attributes");
        if (!attrs.empty()) {
            t.expect(attrs[0].arguments.size() == 2, "template spacing regression: template argument count");
            if (attrs[0].arguments.size() == 2) {
                t.expect(normalize_ws(attrs[0].arguments[1]) == "std::pair<int,int>",
                         "template spacing regression: template type preserved");
            }
        }
    }

    // Regression: qualifiers after closing angle must still keep a single template argument.
    {
        auto attrs = parse_attribute_list(R"(template(T, std::pair<int, int> const&), test("templated-qual"))");
        t.expect(attrs.size() == 2, "template qualifier regression: expected two attributes");
        if (!attrs.empty()) {
            t.expect(attrs[0].arguments.size() == 2, "template qualifier regression: template argument count");
            if (attrs[0].arguments.size() == 2) {
                t.expect(normalize_ws(attrs[0].arguments[1]) == "std::pair<int,int>const&",
                         "template qualifier regression: type preserved");
            }
        }
    }

    // Regression: nested templates with trailing qualifiers should remain a single argument.
    {
        auto attrs = parse_attribute_list(R"(template(T, std::vector<std::array<int,2>> const), test("templated-nested"))");
        t.expect(attrs.size() == 2, "template nested regression: expected two attributes");
        if (!attrs.empty()) {
            t.expect(attrs[0].arguments.size() == 2, "template nested regression: template argument count");
            if (attrs[0].arguments.size() == 2) {
                t.expect(normalize_ws(attrs[0].arguments[1]) == "std::vector<std::array<int,2>>const",
                         "template nested regression: type preserved");
            }
        }
    }

    // Regression: '<' in comparison expressions must not start angle-depth tracking.
    {
        auto attrs = parse_attribute_list(R"(parameters(value, 1<2, 3), test("cmp"))");
        t.expect(attrs.size() == 2, "comparison regression: expected two attributes");
        if (!attrs.empty()) {
            t.expect(attrs[0].name == "parameters", "comparison regression: first attribute name");
            t.expect(attrs[0].arguments.size() == 3, "comparison regression: argument count preserved");
            if (attrs[0].arguments.size() == 3) {
                t.expect(attrs[0].arguments[1] == "1<2", "comparison regression: middle argument preserved");
                t.expect(attrs[0].arguments[2] == "3", "comparison regression: trailing argument preserved");
            }
        }
    }

    // Regression: identifier-based relational expressions must also split correctly.
    {
        auto attrs = parse_attribute_list(R"(parameters(v, a < b, c > d, 42), test("cmp-ident"))");
        t.expect(attrs.size() == 2, "comparison identifier regression: expected two attributes");
        if (!attrs.empty()) {
            t.expect(attrs[0].name == "parameters", "comparison identifier regression: first attribute name");
            t.expect(attrs[0].arguments.size() == 4, "comparison identifier regression: argument count preserved");
            if (attrs[0].arguments.size() == 4) {
                t.expect(attrs[0].arguments[1] == "a < b", "comparison identifier regression: arg1");
                t.expect(attrs[0].arguments[2] == "c > d", "comparison identifier regression: arg2");
                t.expect(attrs[0].arguments[3] == "42", "comparison identifier regression: arg3");
            }
        }
    }

    // Regression: compact identifier comparisons must also split correctly.
    {
        auto attrs = parse_attribute_list(R"(parameters(v, a<b, c>d, 42), test("cmp-ident-compact"))");
        t.expect(attrs.size() == 2, "comparison compact identifier regression: expected two attributes");
        if (!attrs.empty()) {
            t.expect(attrs[0].name == "parameters", "comparison compact identifier regression: first attribute name");
            t.expect(attrs[0].arguments.size() == 4, "comparison compact identifier regression: argument count preserved");
            if (attrs[0].arguments.size() == 4) {
                t.expect(attrs[0].arguments[1] == "a<b", "comparison compact identifier regression: arg1");
                t.expect(attrs[0].arguments[2] == "c>d", "comparison compact identifier regression: arg2");
                t.expect(attrs[0].arguments[3] == "42", "comparison compact identifier regression: arg3");
            }
        }
    }

    // Regression: digit separators must not be treated as char-literal delimiters.
    {
        auto attrs = parse_attribute_list(R"(parameters(v, 1'000, 2), test("digit-sep"))");
        t.expect(attrs.size() == 2, "digit separator regression: expected two attributes");
        if (!attrs.empty()) {
            t.expect(attrs[0].name == "parameters", "digit separator regression: first attribute name");
            t.expect(attrs[0].arguments.size() == 3, "digit separator regression: argument count preserved");
            if (attrs[0].arguments.size() == 3) {
                t.expect(attrs[0].arguments[1] == "1'000", "digit separator regression: middle argument preserved");
                t.expect(attrs[0].arguments[2] == "2", "digit separator regression: trailing argument preserved");
            }
        }
    }

    // Regression: template first argument can be non-type literals.
    {
        auto attrs = parse_attribute_list(R"(template(T, Mat<3,4>), test("templated-nontype"))");
        t.expect(attrs.size() == 2, "template non-type regression: expected two attributes");
        if (!attrs.empty()) {
            t.expect(attrs[0].arguments.size() == 2, "template non-type regression: template argument count");
            if (attrs[0].arguments.size() == 2) {
                t.expect(normalize_ws(attrs[0].arguments[1]) == "Mat<3,4>", "template non-type regression: value preserved");
            }
        }
    }

    // Regression: char literals containing '>' must not terminate template parsing.
    {
        auto attrs = parse_attribute_list(R"(template(T, std::integral_constant<char, '>'>), test("templated-char-literal"))");
        t.expect(attrs.size() == 2, "template char literal regression: expected two attributes");
        if (!attrs.empty()) {
            t.expect(attrs[0].arguments.size() == 2, "template char literal regression: template argument count");
            if (attrs[0].arguments.size() == 2) {
                t.expect(normalize_ws(attrs[0].arguments[1]) == "std::integral_constant<char,'>'>",
                         "template char literal regression: type preserved");
            }
        }
    }

    // Regression: parameters_pack tuple splitting must also keep '<...>' intact.
    {
        auto attrs =
            parse_attribute_list(R"(test("x"), parameters_pack((value), (std::pair<int, int>{1, 2}), (std::pair<int, int>{3, 4})))");
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

    // Regression: parameters_pack must split comparison rows by commas (not treat '<' as template open).
    {
        auto                     attrs = parse_attribute_list(R"(test("x"), parameters_pack((a,b), (1<2, 3)))");
        std::vector<std::string> diags;
        auto                     summary = validate_attributes(attrs, [&](const std::string &m) { diags.push_back(m); });
        t.expect(!summary.had_error, "parameters_pack comparison regression: no arity mismatch");
        t.expect(summary.param_packs.size() == 1, "parameters_pack comparison regression: one pack");
        if (summary.param_packs.size() == 1) {
            t.expect(summary.param_packs[0].rows.size() == 1, "parameters_pack comparison regression: one row");
            if (summary.param_packs[0].rows.size() == 1) {
                t.expect(summary.param_packs[0].rows[0].size() == 2, "parameters_pack comparison regression: row arity");
            }
        }
    }

    // Regression: parameters_pack identifier comparisons should split into two cells.
    {
        auto                     attrs = parse_attribute_list(R"(test("x"), parameters_pack((a,b), (x < y, z > w)))");
        std::vector<std::string> diags;
        auto                     summary = validate_attributes(attrs, [&](const std::string &m) { diags.push_back(m); });
        t.expect(!summary.had_error, "parameters_pack identifier comparison regression: no arity mismatch");
        t.expect(summary.param_packs.size() == 1, "parameters_pack identifier comparison regression: one pack");
        if (summary.param_packs.size() == 1) {
            t.expect(summary.param_packs[0].rows.size() == 1, "parameters_pack identifier comparison regression: one row");
            if (summary.param_packs[0].rows.size() == 1) {
                t.expect(summary.param_packs[0].rows[0].size() == 2, "parameters_pack identifier comparison regression: row arity");
            }
        }
    }

    // Regression: parameters_pack must split tuples containing digit separators.
    {
        auto                     attrs = parse_attribute_list(R"(test("x"), parameters_pack((a,b), (1'000, 2)))");
        std::vector<std::string> diags;
        auto                     summary = validate_attributes(attrs, [&](const std::string &m) { diags.push_back(m); });
        t.expect(!summary.had_error, "parameters_pack digit separator regression: no validation error");
        t.expect(summary.param_packs.size() == 1, "parameters_pack digit separator regression: one pack");
        if (summary.param_packs.size() == 1) {
            t.expect(summary.param_packs[0].rows.size() == 1, "parameters_pack digit separator regression: one row");
            if (summary.param_packs[0].rows.size() == 1) {
                t.expect(summary.param_packs[0].rows[0].size() == 2, "parameters_pack digit separator regression: row arity");
            }
        }
    }

    // Regression: parameters_pack should accept spaced template syntax in tuple rows.
    {
        auto                     attrs = parse_attribute_list(R"(test("x"), parameters_pack((v), (std::pair <int, int>{1,2})))");
        std::vector<std::string> diags;
        auto                     summary = validate_attributes(attrs, [&](const std::string &m) { diags.push_back(m); });
        t.expect(!summary.had_error, "parameters_pack spacing regression: no validation error");
        t.expect(summary.param_packs.size() == 1, "parameters_pack spacing regression: one pack");
        if (summary.param_packs.size() == 1) {
            t.expect(summary.param_packs[0].rows.size() == 1, "parameters_pack spacing regression: one row");
            if (summary.param_packs[0].rows.size() == 1) {
                t.expect(summary.param_packs[0].rows[0].size() == 1, "parameters_pack spacing regression: row arity");
            }
        }
    }

    // Regression: parameters_pack should accept qualified template expressions in rows.
    {
        auto                     attrs = parse_attribute_list(R"(test("x"), parameters_pack((v), (std::pair<int, int> const{1,2})))");
        std::vector<std::string> diags;
        auto                     summary = validate_attributes(attrs, [&](const std::string &m) { diags.push_back(m); });
        t.expect(!summary.had_error, "parameters_pack qualifier regression: no validation error");
        t.expect(summary.param_packs.size() == 1, "parameters_pack qualifier regression: one pack");
        if (summary.param_packs.size() == 1) {
            t.expect(summary.param_packs[0].rows.size() == 1, "parameters_pack qualifier regression: one row");
            if (summary.param_packs[0].rows.size() == 1) {
                t.expect(summary.param_packs[0].rows[0].size() == 1, "parameters_pack qualifier regression: row arity");
            }
        }
    }

    // Regression: parameters_pack should accept nested templates with qualifier after closing '>>'.
    {
        auto attrs = parse_attribute_list(R"(test("x"), parameters_pack((v), (std::vector<std::array<int,2>> const{1,2})))");
        std::vector<std::string> diags;
        auto                     summary = validate_attributes(attrs, [&](const std::string &m) { diags.push_back(m); });
        t.expect(!summary.had_error, "parameters_pack nested qualifier regression: no validation error");
        t.expect(summary.param_packs.size() == 1, "parameters_pack nested qualifier regression: one pack");
        if (summary.param_packs.size() == 1) {
            t.expect(summary.param_packs[0].rows.size() == 1, "parameters_pack nested qualifier regression: one row");
            if (summary.param_packs[0].rows.size() == 1) {
                t.expect(summary.param_packs[0].rows[0].size() == 1, "parameters_pack nested qualifier regression: row arity");
            }
        }
    }

    // Regression: parameters_pack should keep scoped template + declarator rows as a single cell.
    {
        auto                     attrs = parse_attribute_list(R"(test("x"), parameters_pack((v), (std::pair<int, int> value{1,2})))");
        std::vector<std::string> diags;
        auto                     summary = validate_attributes(attrs, [&](const std::string &m) { diags.push_back(m); });
        t.expect(!summary.had_error, "parameters_pack declarator regression: no validation error");
        t.expect(summary.param_packs.size() == 1, "parameters_pack declarator regression: one pack");
        if (summary.param_packs.size() == 1) {
            t.expect(summary.param_packs[0].rows.size() == 1, "parameters_pack declarator regression: one row");
            if (summary.param_packs[0].rows.size() == 1) {
                t.expect(summary.param_packs[0].rows[0].size() == 1, "parameters_pack declarator regression: row arity");
            }
        }
    }

    // Regression: parameters_pack should also keep spaced template declarator rows intact.
    {
        auto                     attrs = parse_attribute_list(R"(test("x"), parameters_pack((v), (std::pair <int, int> value{1,2})))");
        std::vector<std::string> diags;
        auto                     summary = validate_attributes(attrs, [&](const std::string &m) { diags.push_back(m); });
        t.expect(!summary.had_error, "parameters_pack spaced declarator regression: no validation error");
        t.expect(summary.param_packs.size() == 1, "parameters_pack spaced declarator regression: one pack");
        if (summary.param_packs.size() == 1) {
            t.expect(summary.param_packs[0].rows.size() == 1, "parameters_pack spaced declarator regression: one row");
            if (summary.param_packs[0].rows.size() == 1) {
                t.expect(summary.param_packs[0].rows[0].size() == 1, "parameters_pack spaced declarator regression: row arity");
            }
        }
    }

    // Regression: parameters_pack should handle char literals containing '>' in template args.
    {
        auto                     attrs = parse_attribute_list(R"(test("x"), parameters_pack((v), (std::integral_constant<char, '>'>{})))");
        std::vector<std::string> diags;
        auto                     summary = validate_attributes(attrs, [&](const std::string &m) { diags.push_back(m); });
        t.expect(!summary.had_error, "parameters_pack char literal regression: no validation error");
        t.expect(summary.param_packs.size() == 1, "parameters_pack char literal regression: one pack");
        if (summary.param_packs.size() == 1) {
            t.expect(summary.param_packs[0].rows.size() == 1, "parameters_pack char literal regression: one row");
            if (summary.param_packs[0].rows.size() == 1) {
                t.expect(summary.param_packs[0].rows[0].size() == 1, "parameters_pack char literal regression: row arity");
            }
        }
    }

    // Regression: the outer attribute scanner must not treat ')' inside char literals as the end of the attribute.
    {
        auto attrs = parse_attribute_list(R"(template(T, std::integral_constant<char, ')'>), test("templated-char"))");
        t.expect(attrs.size() == 2, "char literal regression: expected two attributes");
        if (!attrs.empty()) {
            t.expect(attrs[0].name == "template", "char literal regression: first attribute name");
            t.expect(attrs[0].arguments.size() == 2, "char literal regression: template argument count");
            if (attrs[0].arguments.size() == 2) {
                t.expect(normalize_ws(attrs[0].arguments[1]) == "std::integral_constant<char,')'>",
                         "char literal regression: template type preserved");
            }
        }
    }

    // Regression: raw strings with ')' inside the payload must not terminate the containing attribute early.
    {
        auto attrs = parse_attribute_list(R"gent(test(R"tag(suite)raw)tag"), owner("ops"))gent");
        t.expect(attrs.size() == 2, "raw string regression: expected two attributes");
        if (!attrs.empty()) {
            t.expect(attrs[0].name == "test", "raw string regression: first attribute name");
            t.expect(attrs[0].arguments.size() == 1, "raw string regression: test argument count");
            if (attrs[0].arguments.size() == 1) {
                t.expect(attrs[0].arguments[0] == R"outer(R"tag(suite)raw)tag")outer",
                         "raw string regression: raw string literal preserved");
            }
        }
    }

    if (t.failures) {
        std::cerr << "Total failures: " << t.failures << "\n";
        return 1;
    }
    return 0;
}
