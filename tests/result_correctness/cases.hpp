#pragma once

#include "gentest/runner.h"
#include "public/gentest_textual_suite_mocks.hpp"

namespace result_correctness {

[[using gentest: test, death]] void body_death_77();
[[using gentest: test, death]] void runtime_skip_death();

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
