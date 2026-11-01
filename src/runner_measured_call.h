#pragma once

#include "runner_context_scope.h"
#include "runner_measured_executor.h"

#include <algorithm>
#include <chrono>
#include <fmt/format.h>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

namespace gentest::runner::detail {

inline void record_runtime_skip_or_default(const std::shared_ptr<gentest::detail::TestContextInfo> &ctxinfo,
                                           std::string_view                                         default_reason) {
    std::string runtime_skip_reason;
    {
        std::scoped_lock lk(ctxinfo->mtx);
        if (ctxinfo->runtime_skip_requested.load(std::memory_order_relaxed)) {
            runtime_skip_reason = ctxinfo->runtime_skip_reason;
        }
    }
    if (!runtime_skip_reason.empty()) {
        gentest::detail::record_bench_error(std::move(runtime_skip_reason));
    } else {
        gentest::detail::record_bench_error(std::string(default_reason));
    }
}

inline void capture_first_failure_location(const std::shared_ptr<gentest::detail::TestContextInfo> &ctxinfo,
                                           MeasurementCaseFailure                                  &failure) {
    if (!failure.source_file.empty() && failure.source_line != 0) {
        return;
    }

    std::scoped_lock lk(ctxinfo->mtx);
    const auto       location = std::ranges::find_if(ctxinfo->failure_locations,
                                                     [](const auto &candidate) { return !candidate.file.empty() && candidate.line != 0; });
    if (location != ctxinfo->failure_locations.end()) {
        failure.source_file = location->file;
        failure.source_line = location->line;
    }
}

inline void finalize_call_phase_failure(const std::shared_ptr<gentest::detail::TestContextInfo> &ctxinfo,
                                        std::string_view default_skip_reason, const std::string &assertion_fallback, bool &had_assert_fail,
                                        MeasurementCaseFailure &failure) {
    capture_first_failure_location(ctxinfo, failure);
    bool        runtime_skip_requested = false;
    std::string runtime_skip_reason;
    {
        std::scoped_lock lk(ctxinfo->mtx);
        runtime_skip_requested = ctxinfo->runtime_skip_requested.load(std::memory_order_relaxed);
        if (runtime_skip_requested) {
            runtime_skip_reason = ctxinfo->runtime_skip_reason;
        }
    }
    if (runtime_skip_requested) {
        if (!runtime_skip_reason.empty()) {
            gentest::detail::record_bench_error(std::move(runtime_skip_reason));
        } else {
            gentest::detail::record_bench_error(std::string(default_skip_reason));
        }
        had_assert_fail = true;
        return;
    }
    std::string first_failure = gentest::detail::first_recorded_failure(ctxinfo);
    if (!first_failure.empty()) {
        gentest::detail::record_bench_error(std::move(first_failure));
        had_assert_fail = true;
        return;
    }
    if (!assertion_fallback.empty()) {
        gentest::detail::record_bench_error(std::string(assertion_fallback));
        had_assert_fail = true;
    }
}

template <typename Clock     = std::chrono::steady_clock, typename BodyFn, typename InterruptedFn,
          typename OnWaiting = gentest::detail::IgnoreAdoptedContextWait>
double run_call_phase_with_context(const gentest::Case &c, std::string_view default_skip_reason, BodyFn &&body,
                                   InterruptedFn &&on_interrupted, bool &had_assert_fail, MeasurementCaseFailure &failure,
                                   OnWaiting on_waiting = {}) {
    using clock     = Clock;
    auto ctxinfo    = gentest::runner::detail::make_active_test_context(c.name);
    had_assert_fail = false;
    std::string assertion_fallback;
    auto        start      = typename clock::time_point{};
    auto        end        = typename clock::time_point{};
    bool        stopped    = false;
    const auto  stop_timer = [&] {
        if (stopped) {
            return;
        }
        end     = clock::now();
        stopped = true;
    };
    {
        gentest::runner::detail::BasicCurrentTestScope<OnWaiting> test_scope(ctxinfo, std::move(on_waiting));
        gentest::detail::BenchPhaseScope                          bench_scope(gentest::detail::BenchPhase::Call);
        try {
            // Bench/jitter per-call timing should measure only the user call body.
            start = clock::now();
            body();
            stop_timer();
        } catch (const gentest::detail::skip_exception &) {
            stop_timer();
            on_interrupted();
            record_runtime_skip_or_default(ctxinfo, default_skip_reason);
            had_assert_fail = true;
        } catch (const gentest::assertion &e) {
            stop_timer();
            on_interrupted();
            assertion_fallback = e.message();
            had_assert_fail    = true;
        } catch (const gentest::failure &e) {
            stop_timer();
            on_interrupted();
            gentest::detail::record_bench_error(e.what());
            had_assert_fail = true;
        } catch (const std::exception &e) {
            stop_timer();
            on_interrupted();
            gentest::detail::record_bench_error(fmt::format("std::exception: {}", e.what()));
            had_assert_fail = true;
        } catch (...) {
            stop_timer();
            on_interrupted();
            gentest::detail::record_bench_error("unknown exception");
            had_assert_fail = true;
        }
    }
    finalize_call_phase_failure(ctxinfo, default_skip_reason, assertion_fallback, had_assert_fail, failure);
    return std::chrono::duration<double>(end - start).count();
}

} // namespace gentest::runner::detail
