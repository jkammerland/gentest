// Regression checks for runner selection, CLI validation and report writers.
// Each mode is registered as its own CTest entry so one failure stays isolated.
#include "../../src/runner_cli.h"
#include "../../src/runner_selector.h"
#include "gentest/runner.h"

#include <array>
#include <cstddef>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using gentest::Case;
using gentest::runner::CliOptions;

int g_failures = 0;

void expect(bool condition, std::string_view what) {
    if (!condition) {
        ++g_failures;
        std::cerr << "FAILED: " << what << '\n';
    }
}

void noop_case(void *) {}

Case make_case(std::string_view name) {
    return Case{
        .name             = name,
        .fn               = &noop_case,
        .file             = __FILE__,
        .line             = __LINE__,
        .is_benchmark     = false,
        .is_jitter        = false,
        .is_baseline      = false,
        .tags             = {},
        .requirements     = {},
        .skip_reason      = {},
        .should_skip      = false,
        .fixture          = {},
        .fixture_lifetime = gentest::FixtureLifetime::None,
        .suite            = "regressions",
    };
}

std::vector<std::string> selected_names(std::span<const Case> cases, const CliOptions &options) {
    std::vector<std::string> names;
    const auto               selection = gentest::runner::select_cases(cases, options);
    for (const auto index : selection.idxs)
        names.emplace_back(cases[index].name);
    return names;
}

// A pattern '*' that lines up with a literal '*' in the name is still a wildcard.
int check_wildcard_literal_star() {
    const std::array cases{make_case("*ab"), make_case("ptr/deref<int*>"), make_case("ptr/deref<long>"), make_case("a*b*c")};
    const auto       filter = [&](const char *pattern) {
        CliOptions options;
        options.filter_pat = pattern;
        return selected_names(cases, options);
    };
    using Names = std::vector<std::string>;
    expect(filter("*b") == Names{"*ab"}, "--filter='*b' selects '*ab'");
    expect(filter("*int*") == Names{"ptr/deref<int*>"}, "--filter='*int*' selects 'ptr/deref<int*>'");
    expect(filter("*c") == Names{"a*b*c"}, "--filter='*c' selects 'a*b*c'");
    expect(filter("a*c") == Names{"a*b*c"}, "--filter='a*c' selects 'a*b*c'");
    expect(filter("*") == Names{"*ab", "ptr/deref<int*>", "ptr/deref<long>", "a*b*c"}, "--filter='*' selects everything");
    expect(filter("?ab") == Names{"*ab"}, "'?' still matches a literal '*'");
    expect(filter("*x").empty(), "a non-matching pattern stays non-matching");

    CliOptions excluded;
    excluded.exclude_patterns = {"*int*", "*b"};
    expect(selected_names(cases, excluded) == Names{"ptr/deref<long>", "a*b*c"}, "--exclude honours the same wildcard rule");
    return g_failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char **argv) {
    const std::string_view mode = argc > 1 ? argv[1] : "";
    if (mode == "wildcard-literal-star")
        return check_wildcard_literal_star();
    std::cerr << "usage: runner_selection_reports <mode> [args]\n";
    return 2;
}
