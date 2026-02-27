#include "gentest/attributes.h"
#include "gentest/detail/bench_stats.h"
#include "gentest/runner.h"
using namespace gentest::asserts;

#include <array>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace unit {

inline void throw_runtime_error() { throw std::runtime_error("boom"); }
inline void no_throw() {}

[[using gentest: test("arithmetic/sum"), fast]]
void sum_is_computed() {
    std::array values{1, 2, 3, 4};
    const auto result = std::accumulate(values.begin(), values.end(), 0);
    EXPECT_EQ(values.size(), std::size_t{4});
    ASSERT_EQ(values.front(), 1, "first element");
    EXPECT_EQ(values.back(), 4, "last element");
    const auto average = static_cast<double>(result) / values.size();
    EXPECT_EQ(result, 10);
    EXPECT_EQ(average, 2.5, "arithmetic mean");
}

[[using gentest: test("approx/absolute")]]
void approx_absolute() {
    using gentest::approx::Approx;
    EXPECT_EQ(3.1415, Approx(3.14).abs(0.01));
    EXPECT_EQ(Approx(10.0).abs(0.5), 10.3);
}

[[using gentest: test("approx/relative")]]
void approx_relative() {
    using gentest::approx::Approx;
    EXPECT_EQ(101.0, Approx(100.0).rel(2.0)); // 1% diff within 2%
    EXPECT_EQ(Approx(200.0).rel(1.0), 198.5); // 0.75% diff within 1%
}

[[using gentest: test("approx/relative_negative")]]
void approx_relative_negative() {
    using gentest::approx::Approx;
    EXPECT_EQ(-101.0, Approx(-100.0).rel(2.0)); // 1% diff within 2%
    EXPECT_EQ(Approx(-200.0).rel(1.0), -198.5); // 0.75% diff within 1%
}

[[using gentest: test("strings/concatenate"), req("#42"), slow]]
void concatenate_strings() {
    std::string greeting = "hello";
    EXPECT_EQ(greeting.size(), std::size_t{5}, "initial size");
    greeting += " world";
    ASSERT_EQ(greeting.size(), std::size_t{11}, "post-concat size");
    EXPECT_EQ(greeting.substr(0, 5), "hello", "prefix");
    EXPECT_EQ(greeting.substr(6), "world", "suffix");
    EXPECT_TRUE(greeting == "hello world");
}

[[using gentest: test("conditions/negate"), linux]]
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

[[using gentest: test("conditions/false_and_relations")]]
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

[[using gentest: fast]]
void default_name_free() {
    EXPECT_TRUE(true);
}

[[using gentest: test("attributes/close_marker_in_string_]]_ok"), fast]]
void attribute_name_with_close_marker_literal() {
    EXPECT_TRUE(true);
}

[[maybe_unused]] constexpr const char *kCloseMarkerAttrParserRawNoise =
    R"gentest(raw "quoted" text [[not_an_attribute and stray ]] plus // and /* markers)gentest";

[[using gentest: test("attributes/close_marker_after_line_comment_]]_ok"), fast]]
// Parser regression: close-marker text in comments should not terminate attribute scanning ]]
void attribute_name_with_close_marker_after_line_comment() {
    EXPECT_TRUE(true);
}

[[using gentest: test("attributes/close_marker_after_block_comment_]]_ok"), fast]]
/* Parser regression: raw-string-like text R"( [[not_attr]] )" is comment noise. */
void attribute_name_with_close_marker_after_block_comment() {
    EXPECT_TRUE(true);
}

[[using gentest: test("exceptions/expect_throw")]]
void expect_throw_simple() {
    EXPECT_THROW(throw_runtime_error(), std::runtime_error);
    EXPECT_THROW(throw 123, int);
}

[[using gentest: test("exceptions/expect_no_throw")]]
void expect_no_throw_simple() {
    EXPECT_NO_THROW(no_throw());
}

[[using gentest: test("exceptions/assert_throw")]]
void assert_throw_simple() {
    ASSERT_THROW(throw_runtime_error(), std::runtime_error);
    EXPECT_TRUE(true, "continues after ASSERT_THROW");
}

[[using gentest: test("exceptions/assert_no_throw")]]
void assert_no_throw_simple() {
    ASSERT_NO_THROW(no_throw());
    EXPECT_TRUE(true, "continues after ASSERT_NO_THROW");
}

struct DefaultNameFixture {
    [[using gentest: fast]]
    void default_name_member() {
        EXPECT_TRUE(true);
    }
};

[[using gentest: test("bench_stats/stats_known")]]
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

[[using gentest: test("bench_stats/hist_bimodal")]]
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

[[using gentest: test("bench_stats/hist_skewed")]]
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
