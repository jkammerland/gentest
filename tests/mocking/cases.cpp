#include "gentest/attributes.h"
#include "gentest/runner.h"
#include "types.h"

#include <vector>

using namespace gentest::asserts;

#include "gentest/mock.h"

namespace mocking {

[[using gentest: test("mocking/interface/returns")]]
void interface_returns() {
    gentest::mock<Calculator> mock_calc;
    gentest::expect(mock_calc, &Calculator::compute).times(1).returns(42);

    Calculator *iface  = &mock_calc;
    const int   result = iface->compute(12, 30);
    EXPECT_EQ(result, 42);
}

[[using gentest: test("mocking/interface/returns_matches")]]
void interface_returns_matches() {
    gentest::mock<Calculator> mock_calc;
    gentest::expect(mock_calc, &Calculator::compute).with(12, 30).returns(42);

    Calculator *iface  = &mock_calc;
    const int   result = iface->compute(12, 30);
    EXPECT_EQ(result, 42);
}

[[using gentest: test("mocking/interface/reset")]]
void interface_reset() {
    gentest::mock<Calculator> mock_calc;
    int                       resets = 0;
    gentest::expect(mock_calc, &Calculator::reset).times(2).invokes([&]() { ++resets; });

    Calculator *iface = &mock_calc;
    iface->reset();
    iface->reset();

    EXPECT_EQ(resets, 2);
}

[[using gentest: test("mocking/concrete/invokes")]]
void concrete_invokes() {
    gentest::mock<Ticker> mock_tick;
    int                   observed = 0;
    gentest::expect(mock_tick, &Ticker::tick).times(3).invokes([&](int v) { observed += v; });

    mock_tick.tick(1);
    mock_tick.tick(2);
    mock_tick.tick(3);

    EXPECT_EQ(observed, 6);
}

[[using gentest: test("mocking/concrete/invokes_matches")]]
void concrete_invokes_matches() {
    gentest::mock<Ticker> mock_tick;
    int                   observed = 0;
    gentest::expect(mock_tick, &Ticker::tick).times(3).with(1).invokes([&](int v) { observed += v; });

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
    gentest::expect(mock_tick, &Ticker::tick).times(2).where_args([](int v){ return v % 2 == 0; }).invokes([&](int v) { sum += v; });

    mock_tick.tick(2);
    mock_tick.tick(4);

    EXPECT_EQ(sum, 6);
}

[[using gentest: test("mocking/concrete/template_member_expect_int")]]
void concrete_template_member_expect_int() {
    gentest::mock<Ticker> mock_tick;
    int                   sum = 0;
    gentest::expect(mock_tick, &Ticker::template tadd<int>).times(2).with(5).invokes([&](int v) { sum += v; });

    mock_tick.tadd(5);
    mock_tick.tadd(5);

    EXPECT_EQ(sum, 10);
}

[[using gentest: test("mocking/crtp/bridge")]]
void crtp_bridge() {
    gentest::mock<DerivedRunner> mock_runner;
    std::vector<int>             captured;
    gentest::expect(mock_runner, &DerivedRunner::handle).times(2).invokes([&](int v) { captured.push_back(v); });

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
    gentest::expect(mock_runner, &DerivedRunner::handle).with(7).times(2).invokes([&](int) { ++count; });

    mock_runner.handle(7);
    mock_runner.handle(7);

    EXPECT_EQ(count, 2);
}

[[using gentest: test("mocking/matchers/eq_any")]]
void matchers_eq_any() {
    using namespace gentest::match;
    gentest::mock<Calculator> mock_calc;
    gentest::expect(mock_calc, &Calculator::compute)
        .times(1)
        .where(Eq(12), Any())
        .returns(300);

    Calculator *iface  = &mock_calc;
    const int   result = iface->compute(12, 999);
    EXPECT_EQ(result, 300);
}

[[using gentest: test("mocking/matchers/in_range")]]
void matchers_in_range() {
    using namespace gentest::match;
    gentest::mock<Ticker> mock_tick;
    int                   count = 0;
    gentest::expect(mock_tick, &Ticker::tick)
        .times(2)
        .where_args(InRange(5, 10))
        .invokes([&](int) { ++count; });

    mock_tick.tick(5);
    mock_tick.tick(10);
    EXPECT_EQ(count, 2);
}

[[using gentest: test("mocking/matchers/not")]]
void matchers_not() {
    using namespace gentest::match;
    gentest::mock<Ticker> mock_tick;
    int                   sum = 0;
    gentest::expect(mock_tick, &Ticker::tick)
        .times(2)
        .where_args(Not(Eq(0)))
        .invokes([&](int v) { sum += v; });

    mock_tick.tick(1);
    mock_tick.tick(2);
    EXPECT_EQ(sum, 3);
}

} // namespace mocking
