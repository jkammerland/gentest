#include "gentest/runner.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <ranges>
#include <span>
#include <string_view>
#include <vector>

namespace {

std::atomic<unsigned> g_sequence{0};
std::atomic<unsigned> g_alpha_runs{0};
std::atomic<unsigned> g_alpha_order{0};
std::atomic<unsigned> g_middle_runs{0};
std::atomic<unsigned> g_middle_order{0};
std::atomic<unsigned> g_zeta_runs{0};
std::atomic<unsigned> g_zeta_order{0};

void record_call(std::atomic<unsigned> &runs, std::atomic<unsigned> &order) {
    runs.fetch_add(1, std::memory_order_relaxed);
    order.store(g_sequence.fetch_add(1, std::memory_order_relaxed) + 1, std::memory_order_relaxed);
}

void alpha_selected(void *) { record_call(g_alpha_runs, g_alpha_order); }
void middle_filtered(void *) { record_call(g_middle_runs, g_middle_order); }
void zeta_unselected(void *) { record_call(g_zeta_runs, g_zeta_order); }

const gentest::Case kCases[] = {
    {
        .name             = "embed/zeta_unselected",
        .fn               = &zeta_unselected,
        .file             = "zeta.cpp",
        .line             = 30,
        .fixture_lifetime = gentest::FixtureLifetime::None,
        .suite            = "embed",
    },
    {
        .name             = "embed/middle_filtered",
        .fn               = &middle_filtered,
        .file             = "middle.cpp",
        .line             = 20,
        .fixture_lifetime = gentest::FixtureLifetime::None,
        .suite            = "embed",
    },
    {
        .name             = "embed/alpha_selected",
        .fn               = &alpha_selected,
        .file             = "alpha.cpp",
        .line             = 10,
        .fixture_lifetime = gentest::FixtureLifetime::None,
        .suite            = "embed",
    },
};

auto case_table() -> std::span<const gentest::Case> { return kCases; }

void reset_calls() {
    g_sequence.store(0, std::memory_order_relaxed);
    g_alpha_runs.store(0, std::memory_order_relaxed);
    g_alpha_order.store(0, std::memory_order_relaxed);
    g_middle_runs.store(0, std::memory_order_relaxed);
    g_middle_order.store(0, std::memory_order_relaxed);
    g_zeta_runs.store(0, std::memory_order_relaxed);
    g_zeta_order.store(0, std::memory_order_relaxed);
}

bool same_case(const gentest::Case &lhs, const gentest::Case &rhs) {
    return lhs.name == rhs.name && lhs.fn == rhs.fn && lhs.file == rhs.file && lhs.line == rhs.line &&
           lhs.is_benchmark == rhs.is_benchmark && lhs.is_jitter == rhs.is_jitter && lhs.is_baseline == rhs.is_baseline &&
           std::ranges::equal(lhs.tags, rhs.tags) && std::ranges::equal(lhs.requirements, rhs.requirements) &&
           lhs.skip_reason == rhs.skip_reason && lhs.should_skip == rhs.should_skip && lhs.fixture == rhs.fixture &&
           lhs.fixture_lifetime == rhs.fixture_lifetime && lhs.suite == rhs.suite && lhs.async_fn == rhs.async_fn &&
           lhs.is_async == rhs.is_async && lhs.items_per_call == rhs.items_per_call && lhs.owner == rhs.owner;
}

bool same_registry(const std::vector<gentest::Case> &lhs, const std::vector<gentest::Case> &rhs) {
    return lhs.size() == rhs.size() && std::ranges::equal(lhs, rhs, same_case);
}

bool contains_manual_case(std::span<const gentest::Case> cases) {
    return std::ranges::any_of(cases, [](const gentest::Case &registered) {
        return std::ranges::any_of(kCases, [&](const gentest::Case &manual) { return registered.name == manual.name; });
    });
}

} // namespace

int main() {
    int  failures = 0;
    auto expect   = [&](bool condition, const char *message) {
        if (condition)
            return;
        std::fprintf(stderr, "run_cases API test failure: %s\n", message);
        ++failures;
    };

    const auto registered_before = gentest::registered_cases();
    expect(!contains_manual_case(registered_before), "manual cases must not start in the global registry");

    reset_calls();
    const char *exact_args[] = {nullptr, "--run=embed/alpha_selected", nullptr, "--kind=test", "--no-color"};
    expect(gentest::run_cases(case_table(), exact_args) == 0, "exact selection with null arguments should succeed");
    expect(g_alpha_runs.load(std::memory_order_relaxed) == 1, "exact selection should run the requested ordinary case");
    expect(g_middle_runs.load(std::memory_order_relaxed) == 0, "exact selection should not run the middle case");
    expect(g_zeta_runs.load(std::memory_order_relaxed) == 0, "exact selection should not run the zeta case");

    reset_calls();
    const char *filter_args[] = {"run-cases-api", "--filter=embed/middle_*", "--kind=test", "--no-color"};
    expect(gentest::run_cases(case_table(), filter_args) == 0, "wildcard filtering should succeed");
    expect(g_alpha_runs.load(std::memory_order_relaxed) == 0, "filtering should exclude the alpha case");
    expect(g_middle_runs.load(std::memory_order_relaxed) == 1, "filtering should run only the matching table case");
    expect(g_zeta_runs.load(std::memory_order_relaxed) == 0, "filtering should exclude the zeta case");

    reset_calls();
    expect(gentest::run_cases(case_table(), std::span<const char *>{}) == 0, "an omitted argument list should run all cases");
    expect(g_alpha_order.load(std::memory_order_relaxed) == 1, "caller cases should use registry-compatible name ordering");
    expect(g_middle_order.load(std::memory_order_relaxed) == 2, "the middle case should run second after sorting");
    expect(g_zeta_order.load(std::memory_order_relaxed) == 3, "the zeta case should run last after sorting");
    expect(kCases[0].name == "embed/zeta_unselected" && kCases[2].name == "embed/alpha_selected",
           "sorting must not mutate the caller-owned table");

    reset_calls();
    const char *missing_value_args[] = {"run-cases-api", "--run", nullptr};
    expect(gentest::run_cases(case_table(), missing_value_args) == 1, "a missing CLI option value should return 1");
    expect(g_sequence.load(std::memory_order_relaxed) == 0, "invalid CLI arguments must not execute cases");

    const char *empty_filter_args[] = {"run-cases-api", "--filter=embed/not_present", "--kind=test", "--no-color"};
    expect(gentest::run_cases(case_table(), empty_filter_args) == 0, "an ordinary filter selecting no cases should return 0");
    expect(g_sequence.load(std::memory_order_relaxed) == 0, "an empty filter selection must not execute cases");

    const char *missing_case_args[] = {"run-cases-api", "--run=embed/not_present", "--kind=test", "--no-color"};
    expect(gentest::run_cases(case_table(), missing_case_args) == 3, "an exact missing case should retain exit code 3");
    expect(gentest::run_cases(std::span<const gentest::Case>{}, std::span<const char *>{}) == 0,
           "an empty table with no selection should return 0");

    const auto registered_after = gentest::registered_cases();
    expect(same_registry(registered_before, registered_after), "run_cases must not alter the global case registry");
    expect(!contains_manual_case(registered_after), "caller-owned cases must not become globally registered");

    if (failures == 0)
        std::puts("run_cases API checks passed");
    return failures == 0 ? 0 : 1;
}
