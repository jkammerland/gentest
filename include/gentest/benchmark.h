#pragma once

#include "gentest/benchmark_types.h"
#include "gentest/detail/runtime_config.h"

#include <chrono>
#include <cmath>
#include <concepts>
#include <limits>
#include <ranges>
#include <source_location>
#include <string_view>
#include <type_traits>
#include <utility>

namespace gentest {

// Once/supplied cases only, on the owner in the measured call or local teardown.
// Exactly one positive finite duration is required. Runtime items replace static items_per_call.
GENTEST_RUNTIME_API void setBenchmarkDuration(std::chrono::duration<double> elapsed,
                                              const std::source_location   &location = std::source_location::current());
GENTEST_RUNTIME_API void setBenchmarkDuration(std::chrono::duration<double> elapsed, std::uint64_t completed_items,
                                              const std::source_location &location = std::source_location::current());

// Local measured-case teardown only. Returns an owning copy, or nullopt while primary timing is unavailable.
[[nodiscard]] GENTEST_RUNTIME_API std::optional<MeasurementSnapshot> currentMeasurement();

namespace detail {
GENTEST_RUNTIME_API bool prepare_timing_publication(std::string_view name, const std::source_location &location);
GENTEST_RUNTIME_API void fail_timing_publication(std::string_view reason, const std::source_location &location);
GENTEST_RUNTIME_API void publish_timing_samples(std::string_view name, std::vector<double> samples, const std::source_location &location);

template <typename T> inline constexpr bool                    is_chrono_duration                                     = false;
template <typename Rep, typename Period> inline constexpr bool is_chrono_duration<std::chrono::duration<Rep, Period>> = true;

template <typename T>
concept TimingNumber = std::is_arithmetic_v<std::remove_cvref_t<T>> && !std::same_as<std::remove_cvref_t<T>, bool>;

template <typename T>
concept TimingDuration = is_chrono_duration<std::remove_cvref_t<T>>;

template <std::ranges::input_range Samples, typename Convert>
void collect_timing_samples(std::string_view name, Samples &&samples, Convert convert, const std::source_location &location) {
    // Check phase/ownership/name before touching a possibly lazy, single-pass range.
    if (!prepare_timing_publication(name, location)) {
        return;
    }

    std::vector<double> converted;
    bool                invalid = false;
#if GENTEST_EXCEPTIONS_ENABLED
    try {
#endif
        for (auto &&sample : samples) {
            // Validate before conversion too: a negative/tiny value may underflow to zero.
            const auto raw = [&] {
                if constexpr (TimingDuration<decltype(sample)>)
                    return sample.count();
                else
                    return sample;
            }();
            const long double ns = convert(sample);
            if (raw < 0 || !std::isfinite(ns) || ns < 0 || ns > std::numeric_limits<double>::max() ||
                (raw != 0 && static_cast<double>(ns) == 0)) {
                invalid = true;
                break;
            }
            converted.push_back(static_cast<double>(ns));
        }
#if GENTEST_EXCEPTIONS_ENABLED
    } catch (...) {
        fail_timing_publication("timing sample iteration or allocation failed", location);
        return;
    }
#endif
    if (invalid) {
        fail_timing_publication("timing samples must be finite, nonnegative and representable in nanoseconds", location);
        return;
    }

    publish_timing_samples(name, std::move(converted), location);
}
} // namespace detail

// Each name is published once, in local benchmark/jitter teardown. Ranges are consumed
// synchronously without retaining references or mutating input; empty series have absent statistics.
template <std::ranges::input_range Samples>
    requires detail::TimingDuration<std::ranges::range_reference_t<Samples>>
void recordTimingSamples(std::string_view name, Samples &&samples, const std::source_location &location = std::source_location::current()) {
    detail::collect_timing_samples(
        name, std::forward<Samples>(samples),
        [](const auto &sample) { return std::chrono::duration<long double, std::nano>(sample).count(); }, location);
}

template <std::ranges::input_range Samples, typename Rep, typename Period>
    requires detail::TimingNumber<std::ranges::range_reference_t<Samples>> && detail::TimingNumber<Rep>
void recordTimingSamples(std::string_view name, Samples &&samples, std::chrono::duration<Rep, Period> unit,
                         const std::source_location &location = std::source_location::current()) {
    // Convert without narrowing unsigned hardware counters to a signed duration representation.
    if (!detail::prepare_timing_publication(name, location)) {
        return;
    }
    const long double unit_ns = std::chrono::duration<long double, std::nano>(unit).count();
    if (!std::isfinite(unit_ns) || unit_ns <= 0) {
        detail::fail_timing_publication("timing sample unit must be positive and finite", location);
        return;
    }

    detail::collect_timing_samples(
        name, std::forward<Samples>(samples), [unit_ns](auto sample) { return static_cast<long double>(sample) * unit_ns; }, location);
}

} // namespace gentest
