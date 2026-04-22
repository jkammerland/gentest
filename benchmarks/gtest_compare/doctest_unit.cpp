#include "doctest_compat.hpp"
#include "gentest/bench_util.h"
#include "gentest/detail/bench_stats.h"

#include <array>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace doctest_unit {

inline void throw_runtime_error() { throw std::runtime_error("boom"); }
inline void no_throw() {}

TEST_CASE("unit/arithmetic/sum") {
    std::array values{1, 2, 3, 4};
    const auto result = std::accumulate(values.begin(), values.end(), 0);
    CHECK_EQ(values.size(), std::size_t{4});
    REQUIRE_EQ(values.front(), 1);
    CHECK_EQ(values.back(), 4);
    const auto average = static_cast<double>(result) / values.size();
    CHECK_EQ(result, 10);
    CHECK(result == 10);
    CHECK_EQ(average, 2.5);
}

TEST_CASE("unit/approx/absolute") {
    CHECK(std::abs(3.1415 - 3.14) <= 0.01);
    CHECK(std::abs(10.3 - 10.0) <= 0.5);
}

TEST_CASE("unit/approx/relative") {
    CHECK(std::abs(101.0 - 100.0) <= std::abs(100.0) * 0.02);
    CHECK(std::abs(198.5 - 200.0) <= std::abs(200.0) * 0.01);
}

TEST_CASE("unit/approx/relative_negative") {
    CHECK(std::abs(-101.0 - -100.0) <= std::abs(-100.0) * 0.02);
    CHECK(std::abs(-198.5 - -200.0) <= std::abs(-200.0) * 0.01);
}

TEST_CASE("unit/strings/concatenate") {
    std::string greeting = "hello";
    CHECK_EQ(greeting.size(), std::size_t{5});
    greeting += " world";
    REQUIRE_EQ(greeting.size(), std::size_t{11});
    CHECK_EQ(greeting.substr(0, 5), "hello");
    CHECK_EQ(greeting.substr(6), "world");
    CHECK(greeting == "hello world");
}

TEST_CASE("unit/conditions/negate") {
    bool flag = false;
    REQUIRE_EQ(flag, false);
    CHECK(!flag);
    CHECK_NE(flag, true);

    flag = !flag;
    REQUIRE(flag);
    CHECK_EQ(flag, true);

    flag = !flag;
    CHECK(!flag);
    CHECK_EQ(flag, false);
}

TEST_CASE("unit/conditions/false_and_relations") {
    CHECK_FALSE(false);
    REQUIRE_FALSE(false);

    CHECK_LT(1, 2);
    CHECK_LE(2, 2);
    CHECK_GT(2, 1);
    CHECK_GE(2, 2);

    REQUIRE_LT(1, 2);
    REQUIRE_LE(2, 2);
    REQUIRE_GT(2, 1);
    REQUIRE_GE(2, 2);
}

TEST_CASE("unit/default_name_free") { CHECK(true); }

TEST_CASE("unit/attributes/close_marker_in_string_]]_ok") { CHECK(true); }

[[maybe_unused]] constexpr const char *kCloseMarkerAttrParserRawNoise =
    R"gentest(raw "quoted" text [[not_an_attribute and stray ]] plus // and /* markers)gentest";

// Parser regression: close-marker text in comments should not terminate attribute scanning ]]
TEST_CASE("unit/attributes/close_marker_after_line_comment_]]_ok") { CHECK(true); }

/* Parser regression: raw-string-like text R"( [[not_attr]] )" is comment noise. */
TEST_CASE("unit/attributes/close_marker_after_block_comment_]]_ok") { CHECK(true); }

TEST_CASE("unit/exceptions/expect_throw") {
    CHECK_THROWS_AS(throw_runtime_error(), std::runtime_error);
    CHECK_THROWS_AS(throw 123, int);
}

TEST_CASE("unit/exceptions/expect_no_throw") { CHECK_NOTHROW(no_throw()); }

TEST_CASE("unit/exceptions/assert_throw") {
    REQUIRE_THROWS_AS(throw_runtime_error(), std::runtime_error);
    CHECK(true);
}

TEST_CASE("unit/exceptions/assert_no_throw") {
    REQUIRE_NOTHROW(no_throw());
    CHECK(true);
}

TEST_CASE("unit/DefaultNameFixture/default_name_member") { CHECK(true); }

TEST_CASE("unit/bench_stats/stats_known") {
    std::vector<double> samples{1, 2, 3, 4, 5};
    const auto          stats = gentest::detail::compute_sample_stats(samples);
    CHECK_EQ(stats.count, std::size_t{5});
    CHECK_EQ(stats.min, 1.0);
    CHECK_EQ(stats.max, 5.0);
    CHECK_EQ(stats.median, 3.0);
    CHECK_EQ(stats.mean, 3.0);
    CHECK(std::abs(stats.p05 - 1.2) <= 0.001);
    CHECK(std::abs(stats.p95 - 4.8) <= 0.001);
    CHECK(std::abs(stats.stddev - std::sqrt(2.0)) <= 0.0001);
}

TEST_CASE("unit/bench_stats/hist_bimodal") {
    std::vector<double> samples{0, 0, 0, 0, 10, 10, 10, 10};
    const auto          hist = gentest::detail::compute_histogram(samples, 4);
    CHECK_EQ(hist.bins.size(), std::size_t{4});
    CHECK_EQ(hist.bins[0].count, std::size_t{4});
    CHECK_EQ(hist.bins[1].count, std::size_t{0});
    CHECK_EQ(hist.bins[2].count, std::size_t{0});
    CHECK_EQ(hist.bins[3].count, std::size_t{4});
    CHECK(std::abs(hist.bins[0].percent - 50.0) <= 0.01);
    CHECK(std::abs(hist.bins[3].percent - 50.0) <= 0.01);
    CHECK(std::abs(hist.bins[3].cumulative_percent - 100.0) <= 0.01);
    CHECK(hist.bins[3].inclusive_hi);
}

TEST_CASE("unit/bench_stats/hist_skewed") {
    std::vector<double> samples{0, 0, 0, 0, 10};
    const auto          hist = gentest::detail::compute_histogram(samples, 2);
    CHECK_EQ(hist.bins.size(), std::size_t{2});
    CHECK_EQ(hist.bins[0].count, std::size_t{4});
    CHECK_EQ(hist.bins[1].count, std::size_t{1});
    CHECK(std::abs(hist.bins[0].percent - 80.0) <= 0.01);
    CHECK(std::abs(hist.bins[1].percent - 20.0) <= 0.01);
    CHECK(std::abs(hist.bins[1].cumulative_percent - 100.0) <= 0.01);
}

TEST_CASE("unit/bench_util/clobber_memory_smoke") {
    int        value     = 7;
    const int &value_ref = value;
    gentest::doNotOptimizeAway(value_ref);
    gentest::clobberMemory();
    CHECK_EQ(value, 7);
}

} // namespace doctest_unit
