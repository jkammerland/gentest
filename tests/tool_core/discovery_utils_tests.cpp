#include "discovery_utils.hpp"

#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using gentest::codegen::TemplateBindingSet;
using gentest::codegen::TemplateParamInfo;
using gentest::codegen::TemplateParamKind;
using gentest::codegen::disc::build_binding_rows;
using gentest::codegen::disc::build_binding_rows_attr_order;
using gentest::codegen::disc::build_template_arg_combos;
using gentest::codegen::disc::build_template_arg_combos_attr_order;
using gentest::codegen::disc::flatten_row_cartesian;
using gentest::codegen::disc::has_matching_angle_close;
using gentest::codegen::disc::is_char_literal_prefix;
using gentest::codegen::disc::is_likely_template_left;
using gentest::codegen::disc::is_likely_template_right;
using gentest::codegen::disc::is_parenthesized_row;
using gentest::codegen::disc::is_word_char;
using gentest::codegen::disc::next_identifier_token;
using gentest::codegen::disc::next_non_space;
using gentest::codegen::disc::parse_parenthesized_row;
using gentest::codegen::disc::previous_non_space;
using gentest::codegen::disc::should_enter_char_literal;
using gentest::codegen::disc::split_top_level_items;
using gentest::codegen::disc::split_top_level_items_result;
using gentest::codegen::disc::trim_ascii_copy;
using gentest::codegen::disc::validate_template_attributes;
using gentest::codegen::disc::validate_template_binding_shape;

namespace {

struct Run {
    int  failures = 0;
    void expect(bool ok, std::string_view msg) {
        if (!ok) {
            ++failures;
            std::cerr << "FAIL: " << msg << "\n";
        }
    }
    void contains(std::string_view haystack, std::string_view needle, std::string_view msg) {
        expect(haystack.find(needle) != std::string_view::npos, msg);
    }
    auto expect_size_at_least(std::size_t actual, std::size_t expected, std::string_view msg) -> bool {
        const bool ok = actual >= expected;
        expect(ok, msg);
        return ok;
    }
    template <typename T> auto expect_non_empty(const T &values, std::string_view msg) -> bool {
        const bool ok = !values.empty();
        expect(ok, msg);
        return ok;
    }
};

auto make_param(TemplateParamKind kind, std::string name, bool is_pack = false, std::string usage = {}) -> TemplateParamInfo {
    TemplateParamInfo info{};
    info.kind           = kind;
    info.name           = std::move(name);
    info.is_pack        = is_pack;
    info.usage_spelling = usage.empty() ? info.name + (is_pack ? "..." : "") : std::move(usage);
    return info;
}

auto make_set(std::string name, std::vector<std::string> candidates) -> TemplateBindingSet {
    TemplateBindingSet set{};
    set.param_name = std::move(name);
    set.candidates = std::move(candidates);
    return set;
}

} // namespace

int main() {
    Run t;

    t.expect(trim_ascii_copy("  abc\t") == "abc", "trim_ascii_copy trims outer whitespace");
    t.expect(trim_ascii_copy(" \t ") == "", "trim_ascii_copy can return empty");

    t.expect(previous_non_space("  alpha  ") == 'a', "previous_non_space finds last non-space");
    t.expect(previous_non_space("   ") == '\0', "previous_non_space returns nul when absent");

    t.expect(next_non_space(" \t beta", 0) == 'b', "next_non_space skips leading whitespace");
    t.expect(next_non_space("abc", 3) == '\0', "next_non_space returns nul at end");

    t.expect(is_likely_template_left('A'), "is_likely_template_left accepts alpha");
    t.expect(is_likely_template_left(']'), "is_likely_template_left accepts closing bracket");
    t.expect(!is_likely_template_left('+'), "is_likely_template_left rejects operator");
    t.expect(is_likely_template_right('7'), "is_likely_template_right accepts alnum");
    t.expect(is_likely_template_right('+'), "is_likely_template_right accepts plus");
    t.expect(!is_likely_template_right('!'), "is_likely_template_right rejects punctuation");
    t.expect(is_word_char('_'), "is_word_char accepts underscore");
    t.expect(!is_word_char('-'), "is_word_char rejects punctuation");

    t.expect(is_char_literal_prefix("L'x'", 1), "is_char_literal_prefix detects wide char prefix");
    t.expect(is_char_literal_prefix("u8'x'", 2), "is_char_literal_prefix detects u8 char prefix");
    t.expect(!is_char_literal_prefix("xu8'x'", 3), "is_char_literal_prefix rejects embedded word prefix");
    t.expect(should_enter_char_literal("L'x'", 1), "should_enter_char_literal accepts prefixed literal");
    t.expect(!should_enter_char_literal("can't", 3), "should_enter_char_literal rejects apostrophe in word");
    t.expect(!should_enter_char_literal("'", 0), "should_enter_char_literal rejects unterminated quote");
    t.expect(should_enter_char_literal("{'x'}", 1), "should_enter_char_literal accepts standalone quote");

    t.expect(next_identifier_token("  const value", 0) == "const", "next_identifier_token skips whitespace");
    t.expect(next_identifier_token("  42", 0).empty(), "next_identifier_token rejects digit start");
    t.expect(next_identifier_token(" _name + tail", 0) == "_name", "next_identifier_token accepts underscore");

    t.expect(has_matching_angle_close("std::vector<int>", 11), "has_matching_angle_close matches simple template");
    t.expect(has_matching_angle_close("Tag<'x'>, tail", 3), "has_matching_angle_close handles char literals");
    t.expect(has_matching_angle_close("Wrap<const T> const", 4), "has_matching_angle_close accepts const follower");
    t.expect(has_matching_angle_close("Wrap<int> volatile", 4), "has_matching_angle_close accepts volatile follower");
    t.expect(has_matching_angle_close("Thing<int> Named {", 5), "has_matching_angle_close accepts identifier followed by brace");
    t.expect(!has_matching_angle_close("a < b", 2), "has_matching_angle_close rejects comparison");
    t.expect(!has_matching_angle_close("Thing<int> value", 5), "has_matching_angle_close rejects non-braced identifier follower");

    {
        const auto split = split_top_level_items_result("alpha, std::pair<int, long>, func('x', \"y,z\"), tail");
        t.expect(split.parts.size() == 4, "split_top_level_items_result preserves top-level item count");
        if (t.expect_size_at_least(split.parts.size(), 3, "split_top_level_items_result exposes nested items")) {
            t.expect(split.parts[1] == "std::pair<int, long>", "split_top_level_items_result keeps template commas");
            t.expect(split.parts[2] == "func('x', \"y,z\")", "split_top_level_items_result keeps nested commas");
        }
        t.expect(!split.had_empty_item, "split_top_level_items_result has no empty item");
    }
    {
        const auto split = split_top_level_items_result("left,,right,");
        t.expect(split.parts.size() == 2, "split_top_level_items_result skips empty tokens");
        t.expect(split.had_empty_item, "split_top_level_items_result detects empty items");
    }
    {
        const auto split = split_top_level_items("value < other, tail");
        t.expect(split.size() == 2, "split_top_level_items handles non-template angle text");
        t.expect(split[0] == "value < other", "split_top_level_items keeps comparison in item");
    }

    {
        const auto parsed = parse_parenthesized_row(" ( left, std::pair<int, int> ) ");
        t.expect(parsed.parts.size() == 2, "parse_parenthesized_row splits enclosed row");
        if (t.expect_size_at_least(parsed.parts.size(), 2, "parse_parenthesized_row exposes enclosed row items")) {
            t.expect(parsed.parts[1] == "std::pair<int, int>", "parse_parenthesized_row keeps template commas");
        }
        t.expect(!parsed.had_empty_item, "parse_parenthesized_row reports no empty item");
    }
    {
        const auto parsed = parse_parenthesized_row(" (value,,tail) ");
        t.expect(parsed.had_empty_item, "parse_parenthesized_row reports empty row entry");
        t.expect(parsed.parts.size() == 2, "parse_parenthesized_row keeps non-empty entries");
    }
    {
        const auto parsed = parse_parenthesized_row("not-a-row");
        t.expect(parsed.parts.size() == 1 && parsed.parts[0] == "not-a-row", "parse_parenthesized_row falls back for plain token");
        t.expect(is_parenthesized_row("(a, b)"), "is_parenthesized_row accepts wrapped row");
        t.expect(!is_parenthesized_row(" a, b "), "is_parenthesized_row rejects plain token");
    }

    {
        std::vector<std::string> diags;
        t.expect(validate_template_binding_shape(make_set("Ts", {"(int, double)", "(char)"}),
                                                 make_param(TemplateParamKind::Type, "Ts", true),
                                                 [&](const std::string &m) { diags.push_back(m); }),
                 "validate_template_binding_shape accepts pack rows");
        t.expect(diags.empty(), "validate_template_binding_shape pack success has no diagnostics");
    }
    {
        std::vector<std::string> diags;
        t.expect(!validate_template_binding_shape(make_set("Ts", {"int"}), make_param(TemplateParamKind::Type, "Ts", true),
                                                  [&](const std::string &m) { diags.push_back(m); }),
                 "validate_template_binding_shape rejects bare pack element");
        if (t.expect_non_empty(diags, "validate_template_binding_shape emits pack-shape diagnostic")) {
            t.contains(diags.front(), "requires parenthesized rows", "validate_template_binding_shape reports pack shape");
        }
    }
    {
        std::vector<std::string> diags;
        t.expect(validate_template_binding_shape(make_set("N", {"(1 + 2)"}), make_param(TemplateParamKind::Value, "N"),
                                                 [&](const std::string &m) { diags.push_back(m); }),
                 "validate_template_binding_shape accepts parenthesized value row");
        t.expect(diags.empty(), "validate_template_binding_shape value row has no diagnostics");
    }
    {
        std::vector<std::string> diags;
        t.expect(!validate_template_binding_shape(make_set("T", {"(int)"}), make_param(TemplateParamKind::Type, "T"),
                                                  [&](const std::string &m) { diags.push_back(m); }),
                 "validate_template_binding_shape rejects parenthesized non-pack type row");
        if (t.expect_non_empty(diags, "validate_template_binding_shape emits non-pack row diagnostic")) {
            t.contains(diags.front(), "does not accept parenthesized rows", "validate_template_binding_shape reports non-pack row");
        }
    }
    {
        std::vector<std::string> diags;
        t.expect(!validate_template_binding_shape(make_set("Ts", {"(int,,double)"}), make_param(TemplateParamKind::Type, "Ts", true),
                                                  [&](const std::string &m) { diags.push_back(m); }),
                 "validate_template_binding_shape rejects empty pack row entry");
        if (t.expect_non_empty(diags, "validate_template_binding_shape emits empty-pack-row diagnostic")) {
            t.contains(diags.front(), "does not accept empty row entries", "validate_template_binding_shape reports empty pack row");
        }
    }

    {
        const auto rows = build_binding_rows(make_set("Ts", {"(int, double)", "(char)"}), true);
        t.expect(rows.size() == 2, "build_binding_rows creates pack rows");
        if (t.expect_size_at_least(rows.size(), 1, "build_binding_rows exposes first pack row")) {
            t.expect(rows[0].size() == 2 && rows[0][1] == "double", "build_binding_rows parses pack row values");
        }
    }
    {
        const auto rows = build_binding_rows(make_set("T", {" int ", " long "}), false);
        t.expect(rows.size() == 2, "build_binding_rows creates scalar rows");
        if (t.expect_size_at_least(rows.size(), 1, "build_binding_rows exposes first scalar row")) {
            t.expect(rows[0].size() == 1 && rows[0][0] == "int", "build_binding_rows trims scalar values");
        }
    }
    {
        const auto rows = build_binding_rows_attr_order(make_set("Attr", {"(int, double)", " value "}));
        t.expect(rows.size() == 2, "build_binding_rows_attr_order keeps row count");
        if (t.expect_size_at_least(rows.size(), 2, "build_binding_rows_attr_order exposes both rows")) {
            t.expect(rows[0].size() == 2, "build_binding_rows_attr_order parses parenthesized row");
            t.expect(rows[1].size() == 1 && rows[1][0] == "value", "build_binding_rows_attr_order trims scalar row");
        }
    }

    {
        const auto flattened = flatten_row_cartesian({});
        t.expect(flattened.size() == 1 && flattened[0].empty(), "flatten_row_cartesian returns identity row for empty axes");
    }
    {
        const std::vector<std::vector<std::vector<std::string>>> axes      = {{{"A"}, {"B"}}, {{"1", "2"}, {"3"}}};
        const auto                                               flattened = flatten_row_cartesian(axes);
        t.expect(flattened.size() == 4, "flatten_row_cartesian computes Cartesian product");
        t.expect(flattened[0].size() == 3, "flatten_row_cartesian concatenates row payloads");
        t.expect(flattened[3][0] == "B" && flattened[3][1] == "3", "flatten_row_cartesian preserves axis order");
    }
    {
        std::vector<std::string>              diags;
        const std::vector<TemplateBindingSet> sets = {
            make_set("T", {"int", "long"}),
            make_set("Ts", {"(char)", "(short, unsigned)"}),
        };
        const std::vector<TemplateParamInfo> params = {
            make_param(TemplateParamKind::Type, "T"),
            make_param(TemplateParamKind::Type, "Ts", true),
        };
        t.expect(validate_template_attributes(sets, params, [&](const std::string &m) { diags.push_back(m); }),
                 "validate_template_attributes accepts matching sets");
        t.expect(diags.empty(), "validate_template_attributes success has no diagnostics");
    }
    {
        std::vector<std::string>              diags;
        const std::vector<TemplateBindingSet> sets  = {make_set("T", {"int"})};
        const std::vector<TemplateParamInfo> params = {make_param(TemplateParamKind::Type, "T"), make_param(TemplateParamKind::Value, "N")};
        t.expect(!validate_template_attributes(sets, params, [&](const std::string &m) { diags.push_back(m); }),
                 "validate_template_attributes rejects missing param set");
        if (t.expect_non_empty(diags, "validate_template_attributes emits missing-set diagnostic")) {
            t.contains(diags.front(), "missing 'template(N, ...)'", "validate_template_attributes reports missing set");
        }
    }
    {
        std::vector<std::string>              diags;
        const std::vector<TemplateBindingSet> sets   = {make_set("T", {"int"}), make_set("Ghost", {"double"})};
        const std::vector<TemplateParamInfo>  params = {make_param(TemplateParamKind::Type, "T")};
        t.expect(!validate_template_attributes(sets, params, [&](const std::string &m) { diags.push_back(m); }),
                 "validate_template_attributes rejects unknown param set");
        if (t.expect_non_empty(diags, "validate_template_attributes emits unknown-set diagnostic")) {
            t.contains(diags.front(), "unknown template parameter 'Ghost'", "validate_template_attributes reports unknown set");
        }
    }
    {
        std::vector<std::string>              diags;
        const std::vector<TemplateBindingSet> sets   = {make_set("Ts", {"int"})};
        const std::vector<TemplateParamInfo>  params = {make_param(TemplateParamKind::Type, "Ts", true)};
        t.expect(!validate_template_attributes(sets, params, [&](const std::string &m) { diags.push_back(m); }),
                 "validate_template_attributes propagates shape failure");
        if (t.expect_non_empty(diags, "validate_template_attributes emits shape-failure diagnostic")) {
            t.contains(diags.front(), "requires parenthesized rows", "validate_template_attributes reports shape failure");
        }
    }

    {
        const std::vector<TemplateBindingSet> sets = {
            make_set("T", {"int", "long"}),
            make_set("Ts", {"(char)", "(short, unsigned)"}),
        };
        const std::vector<TemplateParamInfo> params = {
            make_param(TemplateParamKind::Type, "T"),
            make_param(TemplateParamKind::Type, "Ts", true),
        };
        const auto combos = build_template_arg_combos(sets, params);
        t.expect(combos.size() == 4, "build_template_arg_combos expands declaration-order Cartesian product");
        if (t.expect_size_at_least(combos.size(), 4, "build_template_arg_combos exposes expected combinations")) {
            t.expect(combos[0].size() == 2 && combos[0][0] == "int" && combos[0][1] == "char",
                     "build_template_arg_combos preserves declaration order");
            t.expect(combos[3].size() == 3 && combos[3][0] == "long" && combos[3][1] == "short" && combos[3][2] == "unsigned",
                     "build_template_arg_combos flattens pack rows");
        }
    }
    {
        const std::vector<TemplateBindingSet> sets = {
            make_set("First", {"(A, B)", "C"}),
            make_set("Second", {"D", "(E, F)"}),
        };
        const auto combos = build_template_arg_combos_attr_order(sets);
        t.expect(combos.size() == 4, "build_template_arg_combos_attr_order expands attribute-order rows");
        if (t.expect_size_at_least(combos.size(), 4, "build_template_arg_combos_attr_order exposes expected combinations")) {
            t.expect(combos[0].size() == 3 && combos[0][0] == "A" && combos[0][2] == "D",
                     "build_template_arg_combos_attr_order parses parenthesized first axis");
            t.expect(combos[3].size() == 3 && combos[3][0] == "C" && combos[3][1] == "E" && combos[3][2] == "F",
                     "build_template_arg_combos_attr_order parses parenthesized later axis");
        }
    }

    if (t.failures != 0) {
        std::cerr << "Total failures: " << t.failures << "\n";
        return 1;
    }
    return 0;
}
