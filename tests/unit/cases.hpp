#pragma once

#include "gentest/attributes.h"
#include "gentest/bench_util.h"
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
void sum_is_computed();

[[using gentest: test("approx/absolute")]]
void approx_absolute();

[[using gentest: test("approx/relative")]]
void approx_relative();

[[using gentest: test("approx/relative_negative")]]
void approx_relative_negative();

[[using gentest: test("strings/concatenate"), req("#42"), slow]]
void concatenate_strings();

[[using gentest: test("conditions/negate"), linux]]
void negate_condition();

[[using gentest: test("conditions/false_and_relations")]]
void false_and_relations();

[[gentest::fast]]
void default_name_free();

[[using gentest: test("attributes/close_marker_in_string_]]_ok"), fast]]
void attribute_name_with_close_marker_literal();

[[maybe_unused]] constexpr const char *kCloseMarkerAttrParserRawNoise =
    R"gentest(raw "quoted" text [[not_an_attribute and stray ]] plus // and /* markers)gentest";

[[using gentest: test("attributes/close_marker_after_line_comment_]]_ok"), fast]]
// Parser regression: close-marker text in comments should not terminate attribute scanning ]]
void attribute_name_with_close_marker_after_line_comment();

[[using gentest: test("attributes/close_marker_after_block_comment_]]_ok"), fast]]
/* Parser regression: raw-string-like text R"( [[not_attr]] )" is comment noise. */
void attribute_name_with_close_marker_after_block_comment();

[[maybe_unused]] constexpr auto kDigitSeparatedValueBeforeAttribute = 30'000'000;

[[using gentest: test("attributes/digit_separator_before_attribute"), fast]]
// A digit separator in source or comment text such as 1'000 must not hide this test. ]]
void attribute_after_digit_separator();

[[gentest::test("exceptions/expect_throw")]]
void expect_throw_simple();

[[using gentest: test("exceptions/expect_no_throw")]]
void expect_no_throw_simple();

[[using gentest: test("exceptions/assert_throw")]]
void assert_throw_simple();

[[using gentest: test("exceptions/assert_no_throw")]]
void assert_no_throw_simple();

struct DefaultNameFixture {
    [[gentest::fast]]
    void default_name_member() {
        EXPECT_TRUE(true);
    }
};

[[using gentest: test("bench_stats/stats_known")]]
void bench_stats_known();

[[using gentest: test("bench_stats/hist_bimodal")]]
void bench_stats_hist_bimodal();

[[using gentest: test("bench_stats/hist_skewed")]]
void bench_stats_hist_skewed();

[[using gentest: test("bench_util/clobber_memory_smoke")]]
void bench_util_clobber_memory_smoke();

} // namespace unit
