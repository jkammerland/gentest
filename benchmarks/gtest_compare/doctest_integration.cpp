#include "doctest_compat.hpp"

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace doctest_integration {

namespace math {

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

TEST_CASE("integration/math/fibonacci") {
    std::vector<int> expected{0, 1, 1, 2, 3, 5, 8, 13};
    CHECK_EQ(expected.size(), std::size_t{8});
    int previous = -1;
    for (std::size_t idx = 0; idx < expected.size(); ++idx) {
        const int value = fibonacci(static_cast<int>(idx));
        CHECK_EQ(value, expected[idx]);
        if (idx > 1) {
            const int recurrence = expected[idx - 1] + expected[idx - 2];
            CHECK_EQ(value, recurrence);
        }
        CHECK(value >= previous);
        previous = value;
    }
    CHECK_EQ(fibonacci(7), 13);
}

} // namespace math

namespace registry {

TEST_CASE("integration/registry/map") {
    std::map<std::string, int> index{{"alpha", 1}, {"beta", 2}};
    index.emplace("gamma", 3);
    CHECK_EQ(index.size(), std::size_t{3});
    CHECK(index.contains("beta"));
    CHECK(index.contains("alpha"));
    CHECK_FALSE(index.contains("delta"));
    CHECK_EQ(index.at("gamma"), 3);
}

} // namespace registry

namespace errors {

TEST_CASE("integration/errors/recover") {
    bool caught_invalid_argument = false;
    try {
        static_cast<void>(doctest_integration::math::fibonacci(-1));
        FAIL("expected an exception for negative fibonacci argument");
    } catch (const std::invalid_argument &) {
        caught_invalid_argument = true;
        CHECK(true);
    }
    CHECK(caught_invalid_argument);
}

TEST_CASE("integration/errors/throw" * doctest::skip(true)) { throw std::runtime_error("Expected"); }

} // namespace errors

} // namespace doctest_integration
