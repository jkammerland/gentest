#pragma once

#include "gentest/benchmark.h"
#include "gentest/detail/case_api.h"

#include <span>

namespace gentest::detail {

// Stack-owned occurrence state outlives the separate setup/call/teardown contexts.
// Only the serialized owner accesses it; workers retain their existing fail-fast contract.
struct MeasurementState {
    const Case         &test;
    MeasurementSnapshot snapshot;
    bool                primary_ready      = false;
    bool                duration_supplied  = false;
    bool                publication_failed = false;
};

MeasurementState *&measurement_state();

struct MeasurementScope {
    MeasurementState *previous;
    explicit MeasurementScope(MeasurementState &state) : previous(measurement_state()) { measurement_state() = &state; }
    ~MeasurementScope() { measurement_state() = previous; }
    MeasurementScope(const MeasurementScope &)            = delete;
    MeasurementScope &operator=(const MeasurementScope &) = delete;
};

TimingStatistics timing_statistics(std::span<const double> samples);
std::string_view validate_benchmark_options(const Case &test);

} // namespace gentest::detail
