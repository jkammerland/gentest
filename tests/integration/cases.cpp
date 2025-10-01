#include "gentest/runner.h"

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

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

[[using gentest: test("integration/math/fibonacci"), slow, linux]]
void fibonacci_sequence() {
    std::vector<int> expected{0, 1, 1, 2, 3, 5, 8, 13};
    for (std::size_t idx = 0; idx < expected.size(); ++idx) {
        gentest::expect_eq(fibonacci(static_cast<int>(idx)), expected[idx], "fibonacci value");
    }
}

} // namespace integration::math

namespace integration::registry {

[[using gentest: test("integration/registry/map"), category("containers")]]
void map_behaviour() {
    std::map<std::string, int> index{{"alpha", 1}, {"beta", 2}};
    index.emplace("gamma", 3);
    gentest::expect_eq(index.size(), std::size_t{3}, "map size");
    gentest::expect(index.contains("beta"), "beta must be present");
}

} // namespace integration::registry

namespace integration::errors {

[[using gentest: test("integration/errors/recover"), req("BUG-123"), owner("team-runtime")]]
void detect_and_recover_error() {
    try {
        static_cast<void>(integration::math::fibonacci(-1));
        gentest::fail("expected an exception for negative fibonacci argument");
    } catch (const std::invalid_argument &) { gentest::expect(true, "exception captured"); }
}

[[using gentest: test("integration/errors/throw"), skip("unstable"), windows]]
void throw_error() {
    throw std::runtime_error("Expected");
}

} // namespace integration::errors
