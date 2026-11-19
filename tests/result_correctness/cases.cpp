#include "cases.hpp"

#include "gentest/benchmark.h"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string_view>

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

int exiting_factory() {
    (void)std::fputs("input factory exited\n", stderr);
    std::_Exit(23);
}

DeathInput::operator int() const {
    (void)std::fputs("input conversion exited\n", stderr);
    std::_Exit(23);
}

void                     default_factory(int) {}
void                     explicit_factory(int) {}
void                     implicit_conversion(int) {}
gentest::awaitable<void> async_default_factory(int) { co_return; }
gentest::awaitable<void> async_conversion(int) { co_return; }

ReturningInput::~ReturningInput() { std::_Exit(23); }
void                     return_with_input(const ReturningInput &) {}
gentest::awaitable<void> async_return_with_input(const ReturningInput &) { co_await gentest::async::yield(); }
void                     returns_normally(int) {}
void                     entered_with_input(int value, unsigned size) {
    if (value == 17 && size == sizeof(value))
        std::_Exit(23);
}

inline namespace binding {
void bound_default(bool matches, const Input &input) {
    const bool exact = input.value == 8 && input.precise == 0x1.0000000000001p0 && input.converted == 7 &&
                       ::binding_values::conversions == 1 && std::string_view(input.bytes, 4) == std::string_view("\001f\000x", 4);
    if (exact == matches)
        std::_Exit(23);
}
} // namespace binding

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
    // With one calibration call and one warmup call, fail in the first measured epoch.
    static std::size_t                 calls = 0;
    gentest::mock<mocking::Calculator> calculator;
    gentest::expect(calculator, &mocking::Calculator::reset).times(1);
    if (++calls < 3)
        calculator.reset();
}

void bench_satisfied_mock() {
    gentest::mock<mocking::Calculator> calculator;
    gentest::expect(calculator, &mocking::Calculator::reset).times(1);
    calculator.reset();
}

} // namespace result_correctness

namespace measured_outcomes {

void LateFailure::failOnThirdCall() {
    if (++calls == 3) {
        gentest::fail("");
    }
}

void calibrated_empty(LateFailure &fixture) { fixture.failOnThirdCall(); }
void jitter_empty(LateFailure &fixture) { fixture.failOnThirdCall(); }

void supplied_ok() { gentest::setBenchmarkDuration(std::chrono::milliseconds{5}, 5); }

void supplied_empty() {
    supplied_ok();
    gentest::fail("");
}

namespace {
template <class Policy> void put(int value) {
    gentest::mock<mocking::TraitSink<Policy>> sink;
    EXPECT_CALL(sink, put).with(10);
    static_cast<mocking::TraitSink<Policy> &>(sink).put(value);
}
} // namespace

void bench_noexcept_mismatch() { put<noexcept_policy::NoThrow>(11); }
void jitter_noexcept_mismatch() { put<noexcept_policy::NoThrow>(11); }
void ordinary_mismatch() { put<noexcept_policy::Throw>(11); }
void noexcept_match() { put<noexcept_policy::NoThrow>(10); }

void DeferredMismatch::mismatchOnFourthCall(bool skip_after) {
    if (++calls != 4)
        return;

    put<noexcept_policy::NoThrow>(11);
    if (skip_after)
        gentest::skip("skip after mismatch");
}

void DeferredMismatch::tearDown() {
    const auto snapshot = gentest::currentMeasurement();
    gentest::record_property("attempted_calls", calls);
    gentest::record_property("completed_samples", snapshot ? snapshot->primary.statistics.count : 0);
}

void bench_deferred(DeferredMismatch &fixture, bool skip_after) { fixture.mismatchOnFourthCall(skip_after); }
void jitter_deferred(DeferredMismatch &fixture, bool skip_after) { fixture.mismatchOnFourthCall(skip_after); }

} // namespace measured_outcomes
