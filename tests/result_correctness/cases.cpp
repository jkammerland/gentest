#include "cases.hpp"

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <limits>

namespace result_correctness {

void AbortingSetup::setUp() { std::abort(); }
void FailingTeardown::tearDown() { gentest::expect(false, "teardown failure retains precedence"); }
void AbortingTeardown::tearDown() { std::abort(); }
void pass() {}
void runtime_skip() { gentest::skip("runtime skip"); }
void static_skip() { gentest::fail("static skip body must not run"); }

void failure_then_skip() {
    gentest::expect(false, "failure retains precedence");
    gentest::skip("skip after failure");
}

void skip_with_teardown_failure(FailingTeardown &) { gentest::skip("skip before teardown failure"); }

void body_death() { std::_Exit(23); }
void body_death_77() { std::_Exit(77); }
void runtime_skip_death() { gentest::skip("intentional death-case skip"); }
void setup_death(AbortingSetup &) { std::_Exit(23); }
void teardown_death(AbortingTeardown &) {}

gentest::awaitable<void> async_death() {
    co_await gentest::async::yield();
    std::_Exit(23);
}

void approx_special_values() {
    using gentest::approx::Approx;
    const auto inf = std::numeric_limits<long double>::infinity();
    const auto nan = std::numeric_limits<long double>::quiet_NaN();
    const auto max = std::numeric_limits<long double>::max();

    // Special operands are independent of either tolerance and operand order.
    for (const auto value : {0.0L, -0.0L, 1.0L, -1.0L, inf, -inf}) {
        gentest::expect(value == Approx(value));
        gentest::expect(Approx(value).abs(0) == value);
    }
    for (const auto value : {0.0L, 1.0L, inf, -inf, nan}) {
        gentest::expect(value != Approx(nan).abs(inf).rel(inf));
        gentest::expect(Approx(value).abs(inf).rel(inf) != nan);
    }
    for (const auto value : {inf, -inf}) {
        gentest::expect(value != Approx(1).rel(100));
        gentest::expect(Approx(value).abs(inf) != 1);
    }
    gentest::expect(inf != Approx(-inf).rel(inf));
    gentest::expect(0.0L == Approx(-0.0L));
    gentest::expect(1.0L != Approx(1.01L));
    gentest::expect(1.25L == Approx(1).abs(-0.25L));
    gentest::expect(2.0L == Approx(1).rel(-50));
    gentest::expect(max != Approx(-max).rel(150));
    gentest::expect(max == Approx(-max).rel(200));
}

void late_mock(int operation) {
    gentest::mock<mocking::Calculator> clock;
    auto                               expectation = gentest::expect(clock, &mocking::Calculator::compute);
    expectation.times(1).with(1, 1).returns(7);
    gentest::expect_eq(clock.compute(1, 1), 7);

    // Each path must diagnose the rejected operation while retaining its state.
    switch (operation) {
    case 0: gentest::expect(clock, &mocking::Calculator::compute).times(1).returns(8); break;
    case 1: expectation.times(2); break;
    case 2: expectation.with(2, 2); break;
    case 3: expectation.where_args(gentest::match::Any(), gentest::match::Any()); break;
    case 4: expectation.where_call([](int, int) { return true; }); break;
    case 5: expectation.returns(8); break;
    case 6: expectation.invokes([](int, int) { return 8; }); break;
    case 7: expectation.allow_more(); break;
    case 8: gentest::make_nice(clock); break;
    default: gentest::fail("unsupported late-mock probe operation");
    }
}

void bench_unmet_mock() {
    gentest::mock<mocking::Calculator> calculator;
    gentest::expect(calculator, &mocking::Calculator::reset).times(1);
}

void jitter_unmet_mock() {
    gentest::mock<mocking::Calculator> calculator;
    gentest::expect(calculator, &mocking::Calculator::reset).times(1);
}

void bench_require_unwinds_mock() {
    gentest::mock<mocking::Calculator> calculator;
    gentest::expect(calculator, &mocking::Calculator::reset).times(1);
    gentest::require(false, "bench require unwinds mock");
}

void bench_late_unmet_mock() {
    // Satisfied until calibration reaches a large epoch, then unmet on every call.
    static std::size_t                 calls = 0;
    gentest::mock<mocking::Calculator> calculator;
    gentest::expect(calculator, &mocking::Calculator::reset).times(1);
    if (++calls < 100)
        calculator.reset();
}

void bench_satisfied_mock() {
    gentest::mock<mocking::Calculator> calculator;
    gentest::expect(calculator, &mocking::Calculator::reset).times(1);
    calculator.reset();
}

} // namespace result_correctness
