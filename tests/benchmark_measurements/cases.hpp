#pragma once

#include "gentest/benchmark.h"
#include "gentest/runner.h"

namespace measurement_tests {

extern thread_local int allocations_before_failure;

inline int shared_setups = 0;
struct [[gentest::fixture(global)]] SharedProbe : gentest::FixtureSetup {
    void setUp() override { ++shared_setups; }
};

inline int independent_failures = 0;
inline int local_setups         = 0;
inline int local_teardowns      = 0;

struct Sample : gentest::FixtureSetup, gentest::FixtureTearDown {
    int         calls            = 0;
    int         setups           = 0;
    int         lazy_evaluations = 0;
    std::string mode;
    void        setUp() override;
    void        tearDown() override;
};

[[using gentest: bench("primary/call"), bench_once, bench_time("supplied"), items_per_call(16)]]
void supplied_call(Sample &sample);
[[using gentest: bench("primary/teardown"), bench_once, bench_time("supplied")]]
void supplied_teardown(Sample &sample);
[[using gentest: bench("primary/static_items"), bench_once, bench_time("supplied"), items_per_call(16)]]
void static_items(Sample &sample);
[[using gentest: bench("primary/runner"), bench_once]]
void runner_once(Sample &sample);
[[using gentest: bench("primary/no_warmup"), bench_warmup(0)]]
void no_warmup(Sample &sample);
[[using gentest: jitter("primary/jitter")]]
void jitter(Sample &sample);

[[using gentest: bench("series/valid"), bench_once, bench_time("supplied")]]
void valid_series(Sample &sample);
[[using gentest: bench("invalid/missing"), bench_once, bench_time("supplied")]]
void missing(Sample &sample);
[[using gentest: bench("invalid/duplicate"), bench_once, bench_time("supplied")]]
void duplicate(Sample &sample);
[[using gentest: bench("invalid/zero_duration"), bench_once, bench_time("supplied")]]
void zero_duration(Sample &sample);
[[using gentest: bench("invalid/zero_items"), bench_once, bench_time("supplied")]]
void zero_items(Sample &sample);
[[using gentest: bench("invalid/nan"), bench_once, bench_time("supplied")]]
void nan(Sample &sample);
[[using gentest: bench("invalid/series_negative"), bench_once]]
void series_negative(Sample &sample);
[[using gentest: bench("invalid/series_duplicate"), bench_once]]
void series_duplicate(Sample &sample);
[[using gentest: bench("invalid/series_throw"), bench_once]]
void series_throw(Sample &sample);
[[using gentest: bench("invalid/series_phase"), bench_once]]
void series_phase(Sample &sample);
[[using gentest: bench("invalid/series_overflow"), bench_once]]
void series_overflow(Sample &sample);
[[using gentest: bench("invalid/series_unit"), bench_once]]
void series_unit(Sample &sample);
[[using gentest: bench("invalid/teardown_failure"), bench_once, bench_time("supplied")]]
void teardown_failure(Sample &sample);

[[using gentest: bench("invalid/negative_duration"), bench_once, bench_time("supplied")]]
void negative_duration(Sample &sample);
[[using gentest: bench("invalid/infinite_duration"), bench_once, bench_time("supplied")]]
void infinite_duration(Sample &sample);
[[using gentest: bench("invalid/overflow_duration"), bench_once, bench_time("supplied")]]
void overflow_duration(Sample &sample);
[[using gentest: bench("invalid/runner_supplied"), bench_once]]
void runner_supplied(Sample &sample);
[[using gentest: bench("invalid/series_nonfinite"), bench_once]]
void series_nonfinite(Sample &sample);
[[using gentest: bench("invalid/series_name"), bench_once]]
void series_name(Sample &sample);
[[using gentest: bench("invalid/series_underflow"), bench_once]]
void series_underflow(Sample &sample);
[[using gentest: bench("invalid/series_allocation"), bench_once]]
void series_allocation(Sample &sample);
[[using gentest: bench("invalid/series_summary_allocation"), bench_once]]
void series_summary_allocation(Sample &sample);

} // namespace measurement_tests
