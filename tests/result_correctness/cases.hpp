#pragma once

#include "gentest/runner.h"
#include "public/gentest_textual_suite_mocks.hpp"

#include <cstdlib>
#include <type_traits>

namespace binding_values {
inline int conversions = 0;

struct Converted {
    int value = 0;

    Converted() = default;
    explicit Converted(int n) : value(n) {}

    operator int() const {
        ++conversions;
        return value;
    }
};
} // namespace binding_values

namespace result_correctness {

struct DeathInput {
    operator int() const;
};

int exiting_factory();

[[using gentest: test, death]] void                                         default_factory(int value = exiting_factory());
[[using gentest: test, death, parameters(value, "exiting_factory()")]] void explicit_factory(int value);
[[using gentest: test, death, parameters(value, "DeathInput{}")]] void      implicit_conversion(int value);

[[using gentest: test, death]] gentest::awaitable<void> async_default_factory(int value = exiting_factory());
[[using gentest: test, death, parameters(value, "DeathInput{}")]] gentest::awaitable<void> async_conversion(int value);

// Its destructor runs after a normally returning body.
struct ReturningInput {
    ~ReturningInput();
};

[[using gentest: test, death]] void                     return_with_input(const ReturningInput &input = {});
[[using gentest: test, death]] gentest::awaitable<void> async_return_with_input(const ReturningInput &input = {});

[[using gentest: test, death]] void entered_with_input(int value = 17, unsigned size = sizeof(decltype(value)));
[[using gentest: test, death]] void returns_normally(int value = 17);

template <class T>
[[using gentest: test, death, template(T, int), parameters(value, 17, 18)]]
gentest::awaitable<void> entered_async(T value) {
    co_await gentest::async::yield();
    entered_with_input(value);
}

[[using gentest: test, death]] void body_death_77();
[[using gentest: test, death]] void runtime_skip_death();

inline namespace binding {
struct Input {
    int         value;
    double      precise;
    const char *bytes;
    int         converted;
};

inline int select_input(long) { return 7; }

[[using gentest: test, death, parameters(matches, true, false)]]
void bound_default(bool matches, const Input &input = {.value     = select_input(static_cast<int>(0)) + 1,
                                                       .precise   = 0x1.0000000000001p0,
                                                       .bytes     = "\001"
                                                                    "f\000x",
                                                       .converted = ::binding_values::Converted{select_input(0)}});

inline int select_input(int) { return 99; }
inline int replacement_input(long) { return 99; }

constexpr long choose_template(long) { return 7; }

template <class T = decltype(choose_template(0)), int N = choose_template(0), class Qualified = const T>
[[using gentest: test, death, parameters(matches, true, false)]]
void bound_template(bool matches, T value = 5, Qualified other = 3) {
    const bool exact = std::is_same_v<T, long> && std::is_same_v<Qualified, const long> && N == 7 && value == 5 && other == 3;
    if (matches == exact)
        std::_Exit(23);
}

constexpr int choose_template(int) { return 99; }

// Both later declarations would be viable in the generated helper.
namespace binding_values {
struct Converted {
    int value;

    operator int() const { return 99; }
};
} // namespace binding_values
} // namespace binding

struct AbortingSetup : gentest::FixtureSetup {
    void setUp() override;
};

struct FailingTeardown : gentest::FixtureTearDown {
    void tearDown() override;
};

struct AbortingTeardown : gentest::FixtureTearDown {
    void tearDown() override;
};

[[using gentest: test, death]] void teardown_death(AbortingTeardown &);

[[using gentest: test]] void                   pass();
[[using gentest: test]] void                   runtime_skip();
[[using gentest: test, skip("disabled")]] void static_skip();
[[using gentest: test]] void                   failure_then_skip();
[[using gentest: test]] void                   skip_with_teardown_failure(FailingTeardown &);

[[using gentest: test, death]] void                     body_death();
[[using gentest: test, death]] void                     setup_death(AbortingSetup &);
[[using gentest: test, death]] gentest::awaitable<void> async_death();

[[using gentest: test]] void approx_special_values();

[[using gentest: test, parameters(operation, 0, 1, 2, 3, 4, 5, 6, 7, 8)]] void late_mock(int operation);

// Measured bodies whose mock destructor reports an unmet expectation (#262).
[[using gentest: bench("bench_unmet_mock")]] void           bench_unmet_mock();
[[using gentest: jitter("jitter_unmet_mock")]] void         jitter_unmet_mock();
[[using gentest: bench("bench_require_unwinds_mock")]] void bench_require_unwinds_mock();
[[using gentest: bench("bench_late_unmet_mock")]] void      bench_late_unmet_mock();
[[using gentest: bench("bench_satisfied_mock")]] void       bench_satisfied_mock();

} // namespace result_correctness

namespace measured_outcomes {

// One calibration call and one completed epoch precede the failing call.
struct LateFailure {
    int  calls = 0;
    void failOnThirdCall();
};

[[gentest::bench("calibrated_empty")]] void                                         calibrated_empty(LateFailure &fixture);
[[gentest::jitter("jitter_empty")]] void                                            jitter_empty(LateFailure &fixture);
[[using gentest: bench("supplied_empty"), bench_once, bench_time("supplied")]] void supplied_empty();
[[using gentest: bench("supplied_ok"), bench_once, bench_time("supplied")]] void    supplied_ok();

[[using gentest: bench("bench_noexcept_mismatch"), bench_once]] void bench_noexcept_mismatch();
[[gentest::jitter("jitter_noexcept_mismatch")]] void                 jitter_noexcept_mismatch();
[[using gentest: bench("ordinary_mismatch"), bench_once]] void       ordinary_mismatch();
[[using gentest: bench("noexcept_match"), bench_once]] void          noexcept_match();

struct DeferredMismatch : gentest::FixtureTearDown {
    unsigned calls = 0;
    void     mismatchOnFourthCall(bool skip_after);
    void     tearDown() override;
};

[[using gentest: bench("bench_deferred"), parameters(skip_after, false, true)]]
void bench_deferred(DeferredMismatch &fixture, bool skip_after);
[[using gentest: jitter("jitter_deferred"), parameters(skip_after, false, true)]]
void jitter_deferred(DeferredMismatch &fixture, bool skip_after);

} // namespace measured_outcomes

// Later includes can introduce a macro for an identifier bound by the declaration.
#define select_input replacement_input
