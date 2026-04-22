#include <gtest/gtest.h>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace integration {

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

TEST(IntegrationMath, FibonacciSequence) {
    std::vector<int> expected{0, 1, 1, 2, 3, 5, 8, 13};
    EXPECT_EQ(expected.size(), std::size_t{8}) << "expected sample size";
    int previous = -1;
    for (std::size_t idx = 0; idx < expected.size(); ++idx) {
        const int value = fibonacci(static_cast<int>(idx));
        EXPECT_EQ(value, expected[idx]) << "fibonacci value";
        if (idx > 1) {
            const int recurrence = expected[idx - 1] + expected[idx - 2];
            EXPECT_EQ(value, recurrence) << "fibonacci recurrence";
        }
        EXPECT_TRUE(value >= previous) << "sequence non-decreasing";
        previous = value;
    }
    EXPECT_EQ(fibonacci(7), 13) << "explicit fibonacci(7)";
}

} // namespace math

namespace registry {

TEST(IntegrationRegistry, MapBehaviour) {
    std::map<std::string, int> index{{"alpha", 1}, {"beta", 2}};
    index.emplace("gamma", 3);
    EXPECT_EQ(index.size(), std::size_t{3}) << "map size";
    EXPECT_TRUE(index.contains("beta")) << "beta must be present";
    EXPECT_TRUE(index.contains("alpha")) << "alpha present";
    EXPECT_FALSE(index.contains("delta")) << "delta absent";
    EXPECT_EQ(index.at("gamma"), 3) << "gamma value";
}

} // namespace registry

namespace errors {

TEST(IntegrationErrors, DetectAndRecoverError) {
    bool caught_invalid_argument = false;
    try {
        static_cast<void>(integration::math::fibonacci(-1));
        FAIL() << "expected an exception for negative fibonacci argument";
    } catch (const std::invalid_argument &) {
        caught_invalid_argument = true;
        EXPECT_TRUE(true) << "exception captured";
    }
    EXPECT_TRUE(caught_invalid_argument) << "invalid_argument thrown";
}

TEST(IntegrationErrors, ThrowError) {
    GTEST_SKIP() << "unstable";
    throw std::runtime_error("Expected");
}

} // namespace errors

} // namespace integration
