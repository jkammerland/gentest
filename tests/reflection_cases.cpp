#include "gentest/reflection.h"
#include "gentest/runner.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <ranges>
#include <span>
#include <string_view>

namespace reflection_cases {

unsigned ordinaryRuns{};

[[= gentest::reflection::test("reflection/ordinary").tag("fast").requirement("REFL-1").owner("reflection-team")]] void ordinary() {
    ++ordinaryRuns;
}

[[= gentest::reflection::test("reflection/skipped").skip("demonstrating typed skip metadata")]] void skipped() {}

[[= gentest::reflection::bench("reflection/benchmark").baseline().itemsPerCall(16)]] void benchmark() {}

[[= gentest::reflection::jitter("reflection/jitter")]] void jitter() {}

} // namespace reflection_cases

GENTEST_REFLECT_REGISTER(reflection_cases)

namespace {

auto findCase(std::span<const gentest::Case> cases, std::string_view name) -> const gentest::Case * {
    const auto match = std::ranges::find(cases, name, &gentest::Case::name);
    return match == cases.end() ? nullptr : &*match;
}

auto fail(const char *message) -> int {
    (void)std::fprintf(stderr, "C++26 reflection API test failure: %s\n", message);
    return 1;
}

} // namespace

int main() {
    const auto cases = gentest::registered_cases();
    if (cases.size() != 4)
        return fail("the explicit scope registrar must discover all four annotated functions");

    const auto *ordinary = findCase(cases, "reflection/ordinary");
    if (ordinary == nullptr)
        return fail("the ordinary test was not registered");
    if (ordinary->suite != "reflection" || ordinary->is_benchmark || ordinary->is_jitter)
        return fail("ordinary test kind or suite metadata is incorrect");
    if (!std::ranges::equal(ordinary->tags, std::array<std::string_view, 1>{"fast"}))
        return fail("typed tag metadata was not retained");
    if (!std::ranges::equal(ordinary->requirements, std::array<std::string_view, 1>{"REFL-1"}))
        return fail("typed requirement metadata was not retained");
    if (ordinary->owner != "reflection-team")
        return fail("typed owner metadata was not retained");
    if (ordinary->file.empty() || ordinary->line == 0)
        return fail("reflection source location metadata is missing");

    const auto *skipped = findCase(cases, "reflection/skipped");
    if (skipped == nullptr || !skipped->should_skip || skipped->skip_reason != "demonstrating typed skip metadata")
        return fail("typed skip metadata was not retained");

    const auto *benchmark = findCase(cases, "reflection/benchmark");
    if (benchmark == nullptr || !benchmark->is_benchmark || benchmark->is_jitter || !benchmark->is_baseline ||
        benchmark->items_per_call != 16)
        return fail("typed benchmark metadata was not retained");

    const auto *jitter = findCase(cases, "reflection/jitter");
    if (jitter == nullptr || jitter->is_benchmark || !jitter->is_jitter)
        return fail("typed jitter metadata was not retained");

    const char *arguments[] = {"gentest_reflection_cases", "--run=reflection/ordinary", "--kind=test", "--no-color"};
    if (gentest::run_all_tests(std::span<const char *>{arguments}) != 0)
        return fail("the reflected ordinary test failed through the normal runner");
    if (reflection_cases::ordinaryRuns != 1)
        return fail("the reflected wrapper did not invoke the ordinary test exactly once");
    return 0;
}
