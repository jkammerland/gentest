#include "cases.hpp"

namespace unit {

void sum_is_computed() {
    std::array values{1, 2, 3, 4};
    const auto result = std::accumulate(values.begin(), values.end(), 0);
    EXPECT_EQ(values.size(), std::size_t{4});
    ASSERT_EQ(values.front(), 1, "first element");
    EXPECT_EQ(values.back(), 4, "last element");
    const auto average = static_cast<double>(result) / values.size();
    EXPECT_EQ(result, 10);
    gentest::expect_true(result == 10, "expect_true aliases expect");
    EXPECT_EQ(average, 2.5, "arithmetic mean");
}

} // namespace unit

namespace unit {

void approx_absolute() {
    using gentest::approx::Approx;
    EXPECT_EQ(3.1415, Approx(3.14).abs(0.01));
    EXPECT_EQ(Approx(10.0).abs(0.5), 10.3);
}

} // namespace unit

namespace unit {

void approx_relative() {
    using gentest::approx::Approx;
    EXPECT_EQ(101.0, Approx(100.0).rel(2.0)); // 1% diff within 2%
    EXPECT_EQ(Approx(200.0).rel(1.0), 198.5); // 0.75% diff within 1%
}

} // namespace unit

namespace unit {

void approx_relative_negative() {
    using gentest::approx::Approx;
    EXPECT_EQ(-101.0, Approx(-100.0).rel(2.0)); // 1% diff within 2%
    EXPECT_EQ(Approx(-200.0).rel(1.0), -198.5); // 0.75% diff within 1%
}

} // namespace unit

namespace unit {

void concatenate_strings() {
    std::string greeting = "hello";
    EXPECT_EQ(greeting.size(), std::size_t{5}, "initial size");
    greeting += " world";
    ASSERT_EQ(greeting.size(), std::size_t{11}, "post-concat size");
    EXPECT_EQ(greeting.substr(0, 5), "hello", "prefix");
    EXPECT_EQ(greeting.substr(6), "world", "suffix");
    EXPECT_TRUE(greeting == "hello world");
}

} // namespace unit

namespace unit {

void negate_condition() {
    bool flag = false;
    ASSERT_EQ(flag, false, "starts false");
    EXPECT_TRUE(!flag);
    EXPECT_NE(flag, true);

    flag = !flag;
    ASSERT_TRUE(flag, "negation flips to true");
    EXPECT_EQ(flag, true, "flag now true");

    flag = !flag;
    EXPECT_TRUE(!flag);
    EXPECT_EQ(flag, false, "double negation");
}

} // namespace unit

namespace unit {

void false_and_relations() {
    EXPECT_FALSE(false);
    ASSERT_FALSE(false, "still false");

    EXPECT_LT(1, 2);
    EXPECT_LE(2, 2);
    EXPECT_GT(2, 1);
    EXPECT_GE(2, 2);

    ASSERT_LT(1, 2);
    ASSERT_LE(2, 2);
    ASSERT_GT(2, 1);
    ASSERT_GE(2, 2);
}

} // namespace unit

namespace unit {

void default_name_free() { EXPECT_TRUE(true); }

} // namespace unit

namespace unit {

void attribute_name_with_close_marker_literal() { EXPECT_TRUE(true); }

} // namespace unit

namespace unit {

void attribute_name_with_close_marker_after_line_comment() { EXPECT_TRUE(true); }

} // namespace unit

namespace unit {

void attribute_name_with_close_marker_after_block_comment() { EXPECT_TRUE(true); }

} // namespace unit

namespace unit {

void attribute_after_digit_separator() { EXPECT_EQ(kDigitSeparatedValueBeforeAttribute, 30'000'000); }

} // namespace unit

namespace unit {

void expect_throw_simple() {
    EXPECT_THROW(throw_runtime_error(), std::runtime_error);
    EXPECT_THROW(throw 123, int);
}

} // namespace unit

namespace unit {

void expect_no_throw_simple() { EXPECT_NO_THROW(no_throw()); }

} // namespace unit

namespace unit {

void assert_throw_simple() {
    ASSERT_THROW(throw_runtime_error(), std::runtime_error);
    EXPECT_TRUE(true, "continues after ASSERT_THROW");
}

} // namespace unit

namespace unit {

void assert_no_throw_simple() {
    ASSERT_NO_THROW(no_throw());
    EXPECT_TRUE(true, "continues after ASSERT_NO_THROW");
}

} // namespace unit

namespace unit {

void bench_stats_known() {
    std::vector<double> samples{1, 2, 3, 4, 5};
    const auto          stats = gentest::detail::compute_sample_stats(samples);
    EXPECT_EQ(stats.count, std::size_t{5});
    EXPECT_EQ(stats.min, 1.0);
    EXPECT_EQ(stats.max, 5.0);
    EXPECT_EQ(stats.median, 3.0);
    EXPECT_EQ(stats.mean, 3.0);
    using gentest::approx::Approx;
    EXPECT_EQ(stats.p05, Approx(1.2).abs(0.001));
    EXPECT_EQ(stats.p95, Approx(4.8).abs(0.001));
    EXPECT_EQ(stats.stddev, Approx(std::sqrt(2.0)).abs(0.0001));
}

} // namespace unit

namespace unit {

void bench_stats_hist_bimodal() {
    std::vector<double> samples{0, 0, 0, 0, 10, 10, 10, 10};
    const auto          hist = gentest::detail::compute_histogram(samples, 4);
    EXPECT_EQ(hist.bins.size(), std::size_t{4});
    EXPECT_EQ(hist.bins[0].count, std::size_t{4});
    EXPECT_EQ(hist.bins[1].count, std::size_t{0});
    EXPECT_EQ(hist.bins[2].count, std::size_t{0});
    EXPECT_EQ(hist.bins[3].count, std::size_t{4});
    using gentest::approx::Approx;
    EXPECT_EQ(hist.bins[0].percent, Approx(50.0).abs(0.01));
    EXPECT_EQ(hist.bins[3].percent, Approx(50.0).abs(0.01));
    EXPECT_EQ(hist.bins[3].cumulative_percent, Approx(100.0).abs(0.01));
    EXPECT_TRUE(hist.bins[3].inclusive_hi);
}

} // namespace unit

namespace unit {

void bench_stats_hist_skewed() {
    std::vector<double> samples{0, 0, 0, 0, 10};
    const auto          hist = gentest::detail::compute_histogram(samples, 2);
    EXPECT_EQ(hist.bins.size(), std::size_t{2});
    EXPECT_EQ(hist.bins[0].count, std::size_t{4});
    EXPECT_EQ(hist.bins[1].count, std::size_t{1});
    using gentest::approx::Approx;
    EXPECT_EQ(hist.bins[0].percent, Approx(80.0).abs(0.01));
    EXPECT_EQ(hist.bins[1].percent, Approx(20.0).abs(0.01));
    EXPECT_EQ(hist.bins[1].cumulative_percent, Approx(100.0).abs(0.01));
}

} // namespace unit

namespace unit {

void bench_util_clobber_memory_smoke() {
    int        value     = 7;
    const int &value_ref = value;
    gentest::doNotOptimizeAway(value_ref);
    gentest::clobberMemory();
    EXPECT_EQ(value, 7);
}

} // namespace unit
