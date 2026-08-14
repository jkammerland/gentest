#include "cases.hpp"

namespace integration::math {

int fibonacci(int n) {
    if (n < 0) {
        throw std::invalid_argument("negative input not allowed");
    }
    if (n <= 1) {
        return n;
    }
    int a = 0;
    int b = 1;
    for (int i = 2; i <= n; ++i) {
        const int next = a + b;
        a              = b;
        b              = next;
    }
    return b;
}

} // namespace integration::math

namespace integration::math {

void fibonacci_sequence() {
    std::vector<int> expected{0, 1, 1, 2, 3, 5, 8, 13};
    gentest::expect_eq(expected.size(), std::size_t{8}, "expected sample size");
    int previous = -1;
    for (std::size_t idx = 0; idx < expected.size(); ++idx) {
        const int value = fibonacci(static_cast<int>(idx));
        gentest::expect_eq(value, expected[idx], "fibonacci value");
        if (idx > 1) {
            const int recurrence = expected[idx - 1] + expected[idx - 2];
            gentest::expect_eq(value, recurrence, "fibonacci recurrence");
        }
        gentest::expect(value >= previous, "sequence non-decreasing");
        previous = value;
    }
    gentest::expect_eq(fibonacci(7), 13, "explicit fibonacci(7)");
}

} // namespace integration::math

namespace integration::registry {

void map_behaviour() {
    std::map<std::string, int> index{{"alpha", 1}, {"beta", 2}};
    index.emplace("gamma", 3);
    gentest::expect_eq(index.size(), std::size_t{3}, "map size");
    gentest::expect(index.contains("beta"), "beta must be present");
    gentest::expect(index.contains("alpha"), "alpha present");
    gentest::expect(!index.contains("delta"), "delta absent");
    gentest::expect_eq(index.at("gamma"), 3, "gamma value");
}

} // namespace integration::registry

namespace integration::errors {

void detect_and_recover_error() {
    const auto cases = gentest::registered_cases();
    const auto metadata =
        std::ranges::find_if(cases, [](const gentest::Case &test_case) { return test_case.name.ends_with("errors/recover"); });
    gentest::expect(metadata != cases.end(), "generated owner case is present in the Case snapshot");
    if (metadata != cases.end()) {
        gentest::expect_eq(metadata->owner, std::string_view{"team-runtime"}, "owner is available as structured Case metadata");
        gentest::expect(std::ranges::find(metadata->tags, std::string_view{"owner=team-runtime"}) != metadata->tags.end(),
                        "legacy owner tag remains available for tag selection");
    }

    bool caught_invalid_argument = false;
    try {
        static_cast<void>(integration::math::fibonacci(-1));
        gentest::fail("expected an exception for negative fibonacci argument");
    } catch (const std::invalid_argument &) {
        caught_invalid_argument = true;
        gentest::expect(true, "exception captured");
    }
    gentest::expect(caught_invalid_argument, "invalid_argument thrown");
}

} // namespace integration::errors

namespace integration::errors {

void throw_error() { throw std::runtime_error("Expected"); }

} // namespace integration::errors
