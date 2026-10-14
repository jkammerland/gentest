#include "benchmark_internal.h"
#include "gentest/detail/bench_stats.h"
#include "gentest/detail/runtime_support.h"

#include <algorithm>
#include <array>

namespace gentest::detail {

MeasurementState *&measurement_state() {
    thread_local MeasurementState *state = nullptr;
    return state;
}

TimingStatistics timing_statistics(std::span<const double> samples) {
    if (samples.empty()) {
        return {};
    }
    const auto stats = compute_sample_stats(samples);
    return {stats.count, DurationSummary{stats.min, stats.max, stats.mean, stats.median, stats.stddev, stats.p05, stats.p95}};
}

std::string_view validate_benchmark_options(const Case &test) {
    const auto &options = test.benchmark;
    if (options.execution != BenchmarkExecution::Calibrated && options.execution != BenchmarkExecution::Once) {
        return "unknown benchmark execution mode";
    }
    if (options.time_source != BenchmarkTimeSource::Runner && options.time_source != BenchmarkTimeSource::Supplied) {
        return "unknown benchmark time source";
    }
    if (!test.is_benchmark && (options.execution != BenchmarkExecution::Calibrated || options.time_source != BenchmarkTimeSource::Runner ||
                               options.warmup_epochs)) {
        return "benchmark options require a benchmark case";
    }
    if (options.time_source == BenchmarkTimeSource::Supplied && options.execution != BenchmarkExecution::Once) {
        return "supplied timing requires bench_once";
    }
    if (options.execution == BenchmarkExecution::Once && options.warmup_epochs.value_or(0) != 0) {
        return "bench_once cannot declare positive warmup";
    }
    if (test.is_benchmark && test.items_per_call == 0) {
        return "benchmark items_per_call must be positive";
    }
    return {};
}

void fail_timing_publication(std::string_view reason, const std::source_location &location) {
    if (auto *state = measurement_state()) {
        state->publication_failed = true;
    }
    record_failure(failure_text("measurement", location, reason), location);
}

bool prepare_timing_publication(std::string_view name, const std::source_location &location) {
    require_owner_context("recordTimingSamples");
    auto *state = measurement_state();
    if (!state || bench_phase() != BenchPhase::Teardown) {
        fail_timing_publication("timing samples require local benchmark/jitter teardown", location);
        return false;
    }
    if (name.empty()) {
        fail_timing_publication("timing sample name must not be empty", location);
        return false;
    }
    if (std::ranges::any_of(state->snapshot.timings, [name](const auto &entry) { return entry.name == name; })) {
        fail_timing_publication("timing sample name was already published", location);
        return false;
    }
    return true;
}

void publish_timing_samples(std::string_view name, std::vector<double> samples, const std::source_location &location) {
    if (!prepare_timing_publication(name, location)) {
        return;
    }
    try {
        // Construct completely before publication. No partial series survives a failed conversion/allocation.
        NamedTimingStatistics entry{std::string(name), timing_statistics(samples)};
        measurement_state()->snapshot.timings.push_back(std::move(entry));
    } catch (...) { fail_timing_publication("timing sample summary allocation failed", location); }
}

} // namespace gentest::detail

namespace gentest {

void setBenchmarkDuration(std::chrono::duration<double> elapsed, const std::source_location &location) {
    detail::require_owner_context("setBenchmarkDuration");
    const auto *state = detail::measurement_state();
    setBenchmarkDuration(elapsed, state ? state->test.items_per_call : 1, location);
}

void setBenchmarkDuration(std::chrono::duration<double> elapsed, std::uint64_t completed_items, const std::source_location &location) {
    detail::require_owner_context("setBenchmarkDuration");
    auto      *state = detail::measurement_state();
    const auto phase = detail::bench_phase();
    if (!state || state->test.benchmark.execution != BenchmarkExecution::Once ||
        state->test.benchmark.time_source != BenchmarkTimeSource::Supplied ||
        (phase != detail::BenchPhase::Call && phase != detail::BenchPhase::Teardown)) {
        detail::fail_timing_publication("supplied duration requires a once/supplied measured call or local teardown", location);
        return;
    }
    if (state->duration_supplied) {
        detail::fail_timing_publication("benchmark duration was already supplied", location);
        return;
    }

    const double ns = elapsed.count() * 1e9;
    if (!std::isfinite(ns) || ns <= 0 || completed_items == 0) {
        detail::fail_timing_publication("benchmark duration and completed items must be positive and representable", location);
        return;
    }
    auto &primary            = state->snapshot.primary;
    primary.statistics       = {1, DurationSummary{ns, ns, ns, ns, 0, ns, ns}};
    primary.items_per_call   = completed_items;
    primary.elapsed_seconds  = elapsed.count();
    state->duration_supplied = true;
    state->primary_ready     = true;
}

std::optional<MeasurementSnapshot> currentMeasurement() {
    detail::require_owner_context("currentMeasurement");
    auto *state = detail::measurement_state();
    if (!state || detail::bench_phase() != detail::BenchPhase::Teardown) {
        detail::fail_timing_publication("currentMeasurement requires local benchmark/jitter teardown", std::source_location::current());
        return std::nullopt;
    }
    if (!state->primary_ready) {
        return std::nullopt;
    }
    return state->snapshot;
}

} // namespace gentest
