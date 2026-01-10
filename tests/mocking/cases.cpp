#include "gentest/attributes.h"
#include "gentest/runner.h"
#include "helper.hpp" // use mock<T> in non-annotated helper code
#include "types.h"

#include <vector>

using namespace gentest::asserts;

#include "gentest/mock.h"

namespace mocking {

[[using gentest: test("mocking/interface/returns")]]
void interface_returns() {
    gentest::mock<Calculator> mock_calc;
    EXPECT_CALL(mock_calc, compute).times(1).returns(42);

    Calculator *iface  = &mock_calc;
    const int   result = iface->compute(12, 30);
    EXPECT_EQ(result, 42);
}

[[using gentest: test("mocking/interface/returns_ref")]]
void interface_returns_ref() {
    gentest::mock<RefProvider> mock_ref;
    int                        storage = 7;
    EXPECT_CALL(mock_ref, value).times(1).returns_ref(storage);

    RefProvider *iface = &mock_ref;
    int &result = iface->value();
    EXPECT_EQ(&result, &storage);
    result = 9;
    EXPECT_EQ(storage, 9);
}

[[using gentest: test("mocking/interface/returns_matches")]]
void interface_returns_matches() {
    gentest::mock<Calculator> mock_calc;
    EXPECT_CALL(mock_calc, compute).with(12, 30).returns(42);

    Calculator *iface  = &mock_calc;
    const int   result = iface->compute(12, 30);
    EXPECT_EQ(result, 42);
}

[[using gentest: test("mocking/interface/reset")]]
void interface_reset() {
    gentest::mock<Calculator> mock_calc;
    int                       resets = 0;
    EXPECT_CALL(mock_calc, reset).times(2).invokes([&]() { ++resets; });

    Calculator *iface = &mock_calc;
    iface->reset();
    iface->reset();

    EXPECT_EQ(resets, 2);
}

[[using gentest: test("mocking/interface/non_default_ctor")]]
void interface_non_default_ctor() {
#ifdef GENTEST_CODEGEN
    // Codegen parses this TU with a placeholder gentest::mock<T> to discover
    // mockable methods; avoid relying on generated constructors/overrides here.
    gentest::mock<NeedsInit> mock_clock;
    EXPECT_CALL(mock_clock, now).times(1);
#else
    gentest::mock<NeedsInit> mock_clock{5};
    EXPECT_CALL(mock_clock, now).times(1).returns(123);

    NeedsInit *iface = &mock_clock;
    EXPECT_EQ(iface->now(), 123);
#endif
}

[[using gentest: test("mocking/concrete/invokes")]]
void concrete_invokes() {
    gentest::mock<Ticker> mock_tick;
    int                   observed = 0;
    EXPECT_CALL(mock_tick, tick).times(3).invokes([&](int v) { observed += v; });

    mock_tick.tick(1);
    mock_tick.tick(2);
    mock_tick.tick(3);

    EXPECT_EQ(observed, 6);
}

[[using gentest: test("mocking/concrete/non_default_ctor")]]
void concrete_non_default_ctor() {
#ifdef GENTEST_CODEGEN
    gentest::mock<NoDefault> mock_nd{7};
    EXPECT_CALL(mock_nd, work).times(1);
#else
    gentest::mock<NoDefault> mock_nd{7};
    EXPECT_CALL(mock_nd, work).times(1).with(3);

    mock_nd.work(3);
#endif
}

[[using gentest: test("mocking/concrete/static_member")]]
void concrete_static_member() {
#ifdef GENTEST_CODEGEN
    gentest::mock<Ticker> mock_tick;
    EXPECT_CALL(mock_tick, add).times(1);
#else
    gentest::mock<Ticker> mock_tick;
    EXPECT_CALL(mock_tick, add).times(1).returns(123);

    EXPECT_EQ(mock_tick.add(4, 5), 123);
#endif
}

[[using gentest: test("mocking/concrete/invokes_matches")]]
void concrete_invokes_matches() {
    gentest::mock<Ticker> mock_tick;
    int                   observed = 0;
    EXPECT_CALL(mock_tick, tick).times(3).with(1).invokes([&](int v) { observed += v; });

    mock_tick.tick(1);
    mock_tick.tick(1);
    mock_tick.tick(1);

    EXPECT_EQ(observed, 3);
}

[[using gentest: test("mocking/concrete/predicate_match")]]
void concrete_predicate_match() {
    gentest::mock<Ticker> mock_tick;
    int                   sum = 0;
    // Accept only even values
    EXPECT_CALL(mock_tick, tick).times(2).where_args([](int v) { return v % 2 == 0; }).invokes([&](int v) { sum += v; });

    mock_tick.tick(2);
    mock_tick.tick(4);

    EXPECT_EQ(sum, 6);
}

[[using gentest: test("mocking/concrete/template_member_expect_int")]]
void concrete_template_member_expect_int() {
    gentest::mock<Ticker> mock_tick;
    int                   sum = 0;
    EXPECT_CALL(mock_tick, tadd<int>).times(2).with(5).invokes([&](int v) { sum += v; });

    mock_tick.tadd(5);
    mock_tick.tadd(5);

    EXPECT_EQ(sum, 10);
}

[[using gentest: test("mocking/crtp/bridge")]]
void crtp_bridge() {
    gentest::mock<DerivedRunner> mock_runner;
    std::vector<int>             captured;
    EXPECT_CALL(mock_runner, handle).times(2).invokes([&](int v) { captured.push_back(v); });

    mock_runner.handle(7);
    mock_runner.handle(11);

    EXPECT_EQ(captured.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(captured[0], 7);
    EXPECT_EQ(captured[1], 11);
}

[[using gentest: test("mocking/crtp/bridge_matches")]]
void crtp_bridge_matches() {
    gentest::mock<DerivedRunner> mock_runner;
    int                          count = 0;
    EXPECT_CALL(mock_runner, handle).with(7).times(2).invokes([&](int) { ++count; });

    mock_runner.handle(7);
    mock_runner.handle(7);

    EXPECT_EQ(count, 2);
}

[[using gentest: test("mocking/matchers/eq_any")]]
void matchers_eq_any() {
    using namespace gentest::match;
    gentest::mock<Calculator> mock_calc;
    EXPECT_CALL(mock_calc, compute).times(1).where(Eq(12), Any()).returns(300);

    Calculator *iface  = &mock_calc;
    const int   result = iface->compute(12, 999);
    EXPECT_EQ(result, 300);
}

[[using gentest: test("mocking/matchers/in_range")]]
void matchers_in_range() {
    using namespace gentest::match;
    gentest::mock<Ticker> mock_tick;
    int                   count = 0;
    EXPECT_CALL(mock_tick, tick).times(2).where_args(InRange(5, 10)).invokes([&](int) { ++count; });

    mock_tick.tick(5);
    mock_tick.tick(10);
    EXPECT_EQ(count, 2);
}

[[using gentest: test("mocking/matchers/not")]]
void matchers_not() {
    using namespace gentest::match;
    gentest::mock<Ticker> mock_tick;
    int                   sum = 0;
    EXPECT_CALL(mock_tick, tick).times(2).where_args(Not(Eq(0))).invokes([&](int v) { sum += v; });

    mock_tick.tick(1);
    mock_tick.tick(2);
    EXPECT_EQ(sum, 3);
}

[[using gentest: test("mocking/matchers/where_call")]]
void matchers_where_call() {
    gentest::mock<Calculator> mock_calc;
    EXPECT_CALL(mock_calc, compute).times(1).where_call([](int lhs, int rhs) { return ((lhs + rhs) % 2) == 0; }).returns(42);

    Calculator *iface  = &mock_calc;
    const int   result = iface->compute(1, 3); // even sum
    EXPECT_EQ(result, 42);
}

[[using gentest: test("mocking/move_only/with_eq")]]
void move_only_with_eq() {
    gentest::mock<MOConsumer> mock_mo;
    int                       hits = 0;
    EXPECT_CALL(mock_mo, accept).times(1).with(MoveOnly{7}).invokes([&](const MoveOnly &) { ++hits; });

    mock_mo.accept(MoveOnly{7});
    EXPECT_EQ(hits, 1);
}

[[using gentest: test("mocking/matchers/str_contains")]]
void matchers_str_contains() {
    using namespace gentest::match;
    gentest::mock<Stringer> mock_str;
    int                     hits = 0;
    EXPECT_CALL(mock_str, put).times(2).where_args(StrContains("abc")).invokes([&](const std::string &) { ++hits; });

    mock_str.put("xxabcxx");
    mock_str.put("abc");
    EXPECT_EQ(hits, 2);
}

[[using gentest: test("mocking/matchers/starts_ends")]]
void matchers_starts_ends() {
    using namespace gentest::match;
    gentest::mock<Stringer> mock_str;
    int                     hits = 0;
    EXPECT_CALL(mock_str, put).times(1).where_args(AllOf(StartsWith("hello"), EndsWith("!"))).invokes([&](const std::string &) { ++hits; });

    mock_str.put("hello world!");
    EXPECT_EQ(hits, 1);
}

[[using gentest: test("mocking/matchers/cstr_null_safe")]]
void matchers_cstr_null_safe() {
    using namespace gentest::match;

    auto contains = StrContains("abc").make<const char *>();
    EXPECT_TRUE(static_cast<bool>(contains.test));
    EXPECT_TRUE(static_cast<bool>(contains.describe));
    EXPECT_TRUE(!contains.test(nullptr));
    EXPECT_TRUE(contains.describe(nullptr).find("null") != std::string::npos);
    EXPECT_TRUE(contains.test("xxabcxx"));

    auto starts = StartsWith("abc").make<const char *>();
    EXPECT_TRUE(static_cast<bool>(starts.test));
    EXPECT_TRUE(static_cast<bool>(starts.describe));
    EXPECT_TRUE(!starts.test(nullptr));
    EXPECT_TRUE(starts.describe(nullptr).find("null") != std::string::npos);
    EXPECT_TRUE(starts.test("abcdef"));

    auto ends = EndsWith("xyz").make<const char *>();
    EXPECT_TRUE(static_cast<bool>(ends.test));
    EXPECT_TRUE(static_cast<bool>(ends.describe));
    EXPECT_TRUE(!ends.test(nullptr));
    EXPECT_TRUE(ends.describe(nullptr).find("null") != std::string::npos);
    EXPECT_TRUE(ends.test("123xyz"));
}

[[using gentest: test("mocking/matchers/near")]]
void matchers_near() {
    using namespace gentest::match;
    gentest::mock<Floater> mock_fl;
    int                    hits = 0;
    EXPECT_CALL(mock_fl, feed).times(2).where_args(Near(3.14, 0.01)).invokes([&](double) { ++hits; });

    mock_fl.feed(3.14);
    mock_fl.feed(3.149);
    EXPECT_EQ(hits, 2);
}

[[using gentest: test("mocking/matchers/ge_anyof")]]
void matchers_ge_anyof() {
    using namespace gentest::match;
    gentest::mock<Ticker> mock_tick;
    int                   count = 0;
    EXPECT_CALL(mock_tick, tick).times(2).where_args(Ge(5)).invokes([&](int) { ++count; });

    mock_tick.tick(5);
    mock_tick.tick(7);
    EXPECT_EQ(count, 2);
}

[[using gentest: test("mocking/nice/unexpected_ok")]]
void nice_unexpected_ok() {
    gentest::mock<Ticker> mock_tick;
    gentest::make_nice(mock_tick, true);
    // No expectations set; unexpected call should be tolerated in nice mode
    mock_tick.tick(123);
    EXPECT_TRUE(true);
}

} // namespace mocking
