#include "gentest/attributes.h"
#include "gentest/runner.h"
#include "types.h"

#include <vector>

using namespace gentest::asserts;

#ifndef GENTEST_BUILDING_MOCKS

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

} // namespace mocking

#endif // !GENTEST_BUILDING_MOCKS
