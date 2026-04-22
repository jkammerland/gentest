#include "gentest/bench_util.h"
#include "gentest/detail/bench_stats.h"

#include <array>
#include <cmath>
#include <gtest/gtest.h>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace unit {

inline void throw_runtime_error() { throw std::runtime_error("boom"); }
inline void no_throw() {}

TEST(UnitArithmetic, SumIsComputed) {
    std::array values{1, 2, 3, 4};
    const auto result = std::accumulate(values.begin(), values.end(), 0);
    EXPECT_EQ(values.size(), std::size_t{4});
    ASSERT_EQ(values.front(), 1) << "first element";
    EXPECT_EQ(values.back(), 4) << "last element";
    const auto average = static_cast<double>(result) / values.size();
    EXPECT_EQ(result, 10);
    EXPECT_TRUE(result == 10) << "expect_true aliases expect";
    EXPECT_EQ(average, 2.5) << "arithmetic mean";
}

TEST(UnitApprox, Absolute) {
    EXPECT_NEAR(3.1415, 3.14, 0.01);
    EXPECT_NEAR(10.3, 10.0, 0.5);
}

TEST(UnitApprox, Relative) {
    EXPECT_LE(std::abs(101.0 - 100.0), std::abs(100.0) * 0.02);
    EXPECT_LE(std::abs(198.5 - 200.0), std::abs(200.0) * 0.01);
}

TEST(UnitApprox, RelativeNegative) {
    EXPECT_LE(std::abs(-101.0 - -100.0), std::abs(-100.0) * 0.02);
    EXPECT_LE(std::abs(-198.5 - -200.0), std::abs(-200.0) * 0.01);
}

TEST(UnitStrings, Concatenate) {
    std::string greeting = "hello";
    EXPECT_EQ(greeting.size(), std::size_t{5}) << "initial size";
    greeting += " world";
    ASSERT_EQ(greeting.size(), std::size_t{11}) << "post-concat size";
    EXPECT_EQ(greeting.substr(0, 5), "hello") << "prefix";
    EXPECT_EQ(greeting.substr(6), "world") << "suffix";
    EXPECT_TRUE(greeting == "hello world");
}

TEST(UnitConditions, Negate) {
    bool flag = false;
    ASSERT_EQ(flag, false) << "starts false";
    EXPECT_TRUE(!flag);
    EXPECT_NE(flag, true);

    flag = !flag;
    ASSERT_TRUE(flag) << "negation flips to true";
    EXPECT_EQ(flag, true) << "flag now true";

    flag = !flag;
    EXPECT_TRUE(!flag);
    EXPECT_EQ(flag, false) << "double negation";
}

TEST(UnitConditions, FalseAndRelations) {
    EXPECT_FALSE(false);
    ASSERT_FALSE(false) << "still false";

    EXPECT_LT(1, 2);
    EXPECT_LE(2, 2);
    EXPECT_GT(2, 1);
    EXPECT_GE(2, 2);

    ASSERT_LT(1, 2);
    ASSERT_LE(2, 2);
    ASSERT_GT(2, 1);
    ASSERT_GE(2, 2);
}

TEST(UnitDefaultName, Free) { EXPECT_TRUE(true); }

TEST(UnitAttributes, CloseMarkerInStringOk) { EXPECT_TRUE(true); }

[[maybe_unused]] constexpr const char *kCloseMarkerAttrParserRawNoise =
    R"gentest(raw "quoted" text [[not_an_attribute and stray ]] plus // and /* markers)gentest";

// Parser regression: close-marker text in comments should not terminate attribute scanning ]]
TEST(UnitAttributes, CloseMarkerAfterLineCommentOk) { EXPECT_TRUE(true); }

/* Parser regression: raw-string-like text R"( [[not_attr]] )" is comment noise. */
TEST(UnitAttributes, CloseMarkerAfterBlockCommentOk) { EXPECT_TRUE(true); }

TEST(UnitExceptions, ExpectThrowSimple) {
    EXPECT_THROW(throw_runtime_error(), std::runtime_error);
    EXPECT_THROW(throw 123, int);
}

TEST(UnitExceptions, ExpectNoThrowSimple) { EXPECT_NO_THROW(no_throw()); }

TEST(UnitExceptions, AssertThrowSimple) {
    ASSERT_THROW(throw_runtime_error(), std::runtime_error);
    EXPECT_TRUE(true) << "continues after ASSERT_THROW";
}

TEST(UnitExceptions, AssertNoThrowSimple) {
    ASSERT_NO_THROW(no_throw());
    EXPECT_TRUE(true) << "continues after ASSERT_NO_THROW";
}

struct DefaultNameFixture : testing::Test {};

TEST_F(DefaultNameFixture, DefaultNameMember) { EXPECT_TRUE(true); }

TEST(UnitBenchStats, StatsKnown) {
    std::vector<double> samples{1, 2, 3, 4, 5};
    const auto          stats = gentest::detail::compute_sample_stats(samples);
    EXPECT_EQ(stats.count, std::size_t{5});
    EXPECT_EQ(stats.min, 1.0);
    EXPECT_EQ(stats.max, 5.0);
    EXPECT_EQ(stats.median, 3.0);
    EXPECT_EQ(stats.mean, 3.0);
    EXPECT_NEAR(stats.p05, 1.2, 0.001);
    EXPECT_NEAR(stats.p95, 4.8, 0.001);
    EXPECT_NEAR(stats.stddev, std::sqrt(2.0), 0.0001);
}

TEST(UnitBenchStats, HistBimodal) {
    std::vector<double> samples{0, 0, 0, 0, 10, 10, 10, 10};
    const auto          hist = gentest::detail::compute_histogram(samples, 4);
    EXPECT_EQ(hist.bins.size(), std::size_t{4});
    EXPECT_EQ(hist.bins[0].count, std::size_t{4});
    EXPECT_EQ(hist.bins[1].count, std::size_t{0});
    EXPECT_EQ(hist.bins[2].count, std::size_t{0});
    EXPECT_EQ(hist.bins[3].count, std::size_t{4});
    EXPECT_NEAR(hist.bins[0].percent, 50.0, 0.01);
    EXPECT_NEAR(hist.bins[3].percent, 50.0, 0.01);
    EXPECT_NEAR(hist.bins[3].cumulative_percent, 100.0, 0.01);
    EXPECT_TRUE(hist.bins[3].inclusive_hi);
}

TEST(UnitBenchStats, HistSkewed) {
    std::vector<double> samples{0, 0, 0, 0, 10};
    const auto          hist = gentest::detail::compute_histogram(samples, 2);
    EXPECT_EQ(hist.bins.size(), std::size_t{2});
    EXPECT_EQ(hist.bins[0].count, std::size_t{4});
    EXPECT_EQ(hist.bins[1].count, std::size_t{1});
    EXPECT_NEAR(hist.bins[0].percent, 80.0, 0.01);
    EXPECT_NEAR(hist.bins[1].percent, 20.0, 0.01);
    EXPECT_NEAR(hist.bins[1].cumulative_percent, 100.0, 0.01);
}

TEST(UnitBenchUtil, ClobberMemorySmoke) {
    int        value     = 7;
    const int &value_ref = value;
    gentest::doNotOptimizeAway(value_ref);
    gentest::clobberMemory();
    EXPECT_EQ(value, 7);
}

} // namespace unit
