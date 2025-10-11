#include "parse_core.hpp"
#include "validate.hpp"

#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using gentest::codegen::ParsedAttribute;
using gentest::codegen::parse_attribute_list;
using gentest::codegen::validate_attributes;

struct Run {
    int failures = 0;
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
        auto attrs = parse_attribute_list(R"(test("suite/a"), slow, linux, req("#1"), group("foo"), owner("bar"))");
        std::vector<std::string> diags;
        auto summary = validate_attributes(attrs, [&](const std::string &m) { diags.push_back(m); });
        t.expect(diags.empty(), "valid attributes produce no diagnostics");
        t.expect(summary.had_error == false, "valid attributes should not error");
        t.expect(summary.case_name && *summary.case_name == "suite/a", "test name parsed");
        auto has = [&](std::string_view needle) {
            for (const auto &s : summary.tags) if (s == needle) return true; return false;
        };
        t.expect(has("slow"), "flag 'slow' present");
        t.expect(has("linux"), "flag 'linux' present");
        t.expect(has("group=foo"), "group value present");
        t.expect(has("owner=bar"), "owner value present");
        t.expect(summary.requirements.size() == 1 && summary.requirements[0] == "#1", "single req present");
    }

    // Range/linspace/geom/logspace parse smoke
    {
        auto attrs = parse_attribute_list(R"(test("x"), range(i, 1, 2, 9), linspace(x, 0.0, 1.0, 5), geom(n, 1, 2, 4), logspace(f, -3, 3, 7))");
        std::vector<std::string> diags;
        auto summary = validate_attributes(attrs, [&](const std::string &m) { diags.push_back(m); });
        t.expect(!summary.had_error, "param generators parse without error");
    }

    {
        auto attrs = parse_attribute_list(R"(test("x"), test("y"))");
        std::vector<std::string> diags;
        auto summary = validate_attributes(attrs, [&](const std::string &m) { diags.push_back(m); });
        t.expect(summary.had_error, "duplicate 'test' errors");
        t.expect(!diags.empty(), "duplicate 'test' reports a diagnostic");
    }

    {
        auto attrs = parse_attribute_list(R"(linux, windows, test("x"))");
        std::vector<std::string> diags;
        auto summary = validate_attributes(attrs, [&](const std::string &m) { diags.push_back(m); });
        t.expect(summary.had_error, "conflicting platform flags error");
        t.expect(!diags.empty(), "conflicting flags report a diagnostic");
    }

    {
        auto attrs = parse_attribute_list(R"(test("x"), req())");
        std::vector<std::string> diags;
        auto summary = validate_attributes(attrs, [&](const std::string &m) { diags.push_back(m); });
        t.expect(summary.had_error, "req without arguments errors");
    }

    {
        auto attrs = parse_attribute_list(R"(test("x"), group("a", "b"))");
        std::vector<std::string> diags;
        auto summary = validate_attributes(attrs, [&](const std::string &m) { diags.push_back(m); });
        t.expect(summary.had_error, "group with 2 args errors");
    }

    {
        auto attrs = parse_attribute_list(R"(test("x"), gpu)");
        std::vector<std::string> diags;
        auto summary = validate_attributes(attrs, [&](const std::string &m) { diags.push_back(m); });
        t.expect(summary.had_error, "unknown gentest flag errors");
    }

    if (t.failures) {
        std::cerr << "Total failures: " << t.failures << "\n";
        return 1;
    }
    return 0;
}
