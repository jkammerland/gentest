#include "runner_measured_executor.h"

#include "gentest/detail/bench_stats.h"
#include "runner_case_invoker.h"
#include "runner_fixture_runtime.h"
#include "runner_measured_format.h"
#include "runner_measured_report.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fmt/format.h>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gentest::runner {
namespace {

double mean_of(const std::vector<double> &v) {
    if (v.empty())
        return 0.0;

    double s = 0;
    for (double x : v)
        s += x;
    return s / static_cast<double>(v.size());
}

double stddev_of(const std::vector<double> &v, double mean) {
    if (v.size() < 2)
        return 0.0;
    double sum = 0.0;
    for (double x : v) {
        const double d = x - mean;
        sum += d * d;
    }
    return std::sqrt(sum / static_cast<double>(v.size()));
}

struct OverheadEstimate {
    double      mean_ns   = 0.0;
    double      stddev_ns = 0.0;
    std::size_t samples   = 0;
};

struct CalibratedEpoch {
    std::size_t iterations = 1;
    std::size_t completed  = 0;
    double      elapsed_s  = 0.0;
    bool        had_assert = false;
};

double percentile_sorted(const std::vector<double> &v, double p) {
    if (v.empty())
        return 0.0;
    if (v.size() == 1)
        return v.front();
    if (p <= 0.0)
        return v.front();
    if (p >= 1.0)
        return v.back();
    const double idx  = p * static_cast<double>(v.size() - 1);
    const auto   lo   = static_cast<std::size_t>(idx);
    const auto   hi   = (lo + 1 < v.size()) ? (lo + 1) : lo;
    const double frac = idx - static_cast<double>(lo);
    return v[lo] + (v[hi] - v[lo]) * frac;
}

void wait_and_flush_test_context(const std::shared_ptr<gentest::detail::TestContextInfo> &ctxinfo) {
    gentest::detail::wait_for_adopted_tokens(ctxinfo);
    gentest::detail::flush_current_buffer_for(ctxinfo.get());
}

void record_runtime_skip_or_default(const std::shared_ptr<gentest::detail::TestContextInfo> &ctxinfo, std::string_view default_reason) {
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

void finalize_call_phase_failure(const std::shared_ptr<gentest::detail::TestContextInfo> &ctxinfo, std::string_view default_skip_reason,
                                 bool &had_assert_fail) {
    wait_and_flush_test_context(ctxinfo);
    if (had_assert_fail)
        return;

    bool        runtime_skip_requested = false;
    std::string runtime_skip_reason;
    std::string first_failure;
    {
        std::scoped_lock lk(ctxinfo->mtx);
        runtime_skip_requested = ctxinfo->runtime_skip_requested.load(std::memory_order_relaxed);
        if (runtime_skip_requested) {
            runtime_skip_reason = ctxinfo->runtime_skip_reason;
        }
        if (!ctxinfo->failures.empty()) {
            first_failure = ctxinfo->failures.front();
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
    if (!first_failure.empty()) {
        gentest::detail::record_bench_error(std::move(first_failure));
        had_assert_fail = true;
    }
}

template <typename BodyFn, typename InterruptedFn>
double run_call_phase_with_context(const gentest::Case &c, std::string_view default_skip_reason, BodyFn &&body,
                                   InterruptedFn &&on_interrupted, bool &had_assert_fail) {
    using clock           = std::chrono::steady_clock;
    auto ctxinfo          = std::make_shared<gentest::detail::TestContextInfo>();
    ctxinfo->display_name = std::string(c.name);
    ctxinfo->active       = true;
    gentest::detail::set_current_test(ctxinfo);
    gentest::detail::BenchPhaseScope bench_scope(gentest::detail::BenchPhase::Call);
    auto                             start = clock::now();
    had_assert_fail                        = false;
    try {
        body();
    } catch (const gentest::detail::skip_exception &) {
        on_interrupted();
        record_runtime_skip_or_default(ctxinfo, default_skip_reason);
        had_assert_fail = true;
    } catch (const gentest::assertion &e) {
        on_interrupted();
        gentest::detail::record_bench_error(e.message());
        had_assert_fail = true;
    } catch (const gentest::failure &e) {
        on_interrupted();
        gentest::detail::record_bench_error(e.what());
        had_assert_fail = true;
    } catch (const std::exception &e) {
        on_interrupted();
        gentest::detail::record_bench_error(std::string("std::exception: ") + e.what());
        had_assert_fail = true;
    } catch (...) {
        on_interrupted();
        gentest::detail::record_bench_error("unknown exception");
        had_assert_fail = true;
    }
    finalize_call_phase_failure(ctxinfo, default_skip_reason, had_assert_fail);
    auto end        = clock::now();
    ctxinfo->active = false;
    gentest::detail::set_current_test(nullptr);
    return std::chrono::duration<double>(end - start).count();
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
double run_epoch_calls(const gentest::Case &c, void *ctx, std::size_t iters, std::size_t &iterations_done, bool &had_assert_fail) {
    iterations_done = 0;
    return run_call_phase_with_context(
        c, "skip requested during benchmark call phase",
        [&] {
            for (std::size_t i = 0; i < iters; ++i) {
                c.fn(ctx);
                iterations_done = i + 1;
            }
        },
        [] {}, had_assert_fail);
}

CalibratedEpoch calibrate_epoch_iterations(const gentest::Case &c, void *ctx, const BenchConfig &cfg) {
    CalibratedEpoch calibration{};
    while (true) {
        calibration.elapsed_s = run_epoch_calls(c, ctx, calibration.iterations, calibration.completed, calibration.had_assert);
        if (calibration.had_assert || calibration.elapsed_s >= cfg.min_epoch_time_s) {
            return calibration;
        }
        calibration.iterations *= 2;
        if (calibration.iterations == 0 || calibration.iterations > (std::size_t(1) << 30)) {
            return calibration;
        }
    }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
double run_warmup_epochs(const gentest::Case &c, void *ctx, std::size_t iters, std::size_t warmup_epochs, std::size_t &iterations_done,
                         bool &had_assert_fail) {
    if (had_assert_fail) {
        return 0.0;
    }
    double warmup_time_s = 0.0;
    for (std::size_t i = 0; i < warmup_epochs; ++i) {
        warmup_time_s += run_epoch_calls(c, ctx, iters, iterations_done, had_assert_fail);
        if (had_assert_fail) {
            break;
        }
    }
    return warmup_time_s;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
double run_jitter_epoch_calls(const gentest::Case &c, void *ctx, std::size_t iters, std::size_t &iterations_done, bool &had_assert_fail,
                              std::vector<double> &samples_ns) {
    using clock     = std::chrono::steady_clock;
    iterations_done = 0;
    return run_call_phase_with_context(
        c, "skip requested during jitter call phase",
        [&] {
            for (std::size_t i = 0; i < iters; ++i) {
                auto start = clock::now();
                c.fn(ctx);
                auto end = clock::now();
                samples_ns.push_back(ns_from_s(std::chrono::duration<double>(end - start).count()));
                iterations_done = i + 1;
            }
        },
        [] {}, had_assert_fail);
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
double run_jitter_batch_epoch_calls(const gentest::Case &c, void *ctx, std::size_t batch_iters, std::size_t batch_samples,
                                    std::size_t &iterations_done, bool &had_assert_fail, std::vector<double> &samples_ns) {
    using clock             = std::chrono::steady_clock;
    iterations_done         = 0;
    std::size_t local_done  = 0;
    auto        batch_start = clock::now();
    bool        in_batch    = false;
    return run_call_phase_with_context(
        c, "skip requested during jitter call phase",
        [&] {
            for (std::size_t sample = 0; sample < batch_samples; ++sample) {
                batch_start = clock::now();
                local_done  = 0;
                in_batch    = true;
                for (std::size_t i = 0; i < batch_iters; ++i) {
                    c.fn(ctx);
                    ++local_done;
                }
                auto end = clock::now();
                if (local_done != 0) {
                    samples_ns.push_back(ns_from_s(std::chrono::duration<double>(end - batch_start).count()) /
                                         static_cast<double>(local_done));
                    iterations_done += local_done;
                }
                in_batch = false;
            }
        },
        [&] {
            if (in_batch && local_done != 0) {
                auto end = clock::now();
                samples_ns.push_back(ns_from_s(std::chrono::duration<double>(end - batch_start).count()) / static_cast<double>(local_done));
                iterations_done += local_done;
            }
        },
        had_assert_fail);
}
// NOLINTEND(bugprone-easily-swappable-parameters)

OverheadEstimate estimate_timer_overhead_per_iter(std::size_t sample_count) {
    using clock = std::chrono::steady_clock;
    OverheadEstimate est{};
    if (sample_count == 0)
        return est;
    constexpr std::size_t repeat = 128;
    std::vector<double>   samples;
    samples.reserve(sample_count);
    for (std::size_t i = 0; i < sample_count; ++i) {
        auto start = clock::now();
        for (std::size_t r = 0; r < repeat; ++r) {
            (void)clock::now();
            (void)clock::now();
        }
        auto         end = clock::now();
        const double ns  = ns_from_s(std::chrono::duration<double>(end - start).count()) / static_cast<double>(repeat);
        samples.push_back(ns);
    }
    est.mean_ns   = mean_of(samples);
    est.stddev_ns = stddev_of(samples, est.mean_ns);
    est.samples   = samples.size();
    return est;
}

OverheadEstimate estimate_timer_overhead_batch(std::size_t sample_count, std::size_t batch_iters) {
    using clock = std::chrono::steady_clock;
    OverheadEstimate est{};
    if (sample_count == 0 || batch_iters == 0)
        return est;
    std::vector<double> samples;
    samples.reserve(sample_count);
    volatile std::size_t sink = 0;
    for (std::size_t i = 0; i < sample_count; ++i) {
        auto start = clock::now();
        for (std::size_t j = 0; j < batch_iters; ++j) {
            sink += j;
        }
        auto         end = clock::now();
        const double ns  = ns_from_s(std::chrono::duration<double>(end - start).count()) / static_cast<double>(batch_iters);
        samples.push_back(ns);
    }
    (void)sink;
    est.mean_ns   = mean_of(samples);
    est.stddev_ns = stddev_of(samples, est.mean_ns);
    est.samples   = samples.size();
    return est;
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
bool run_measurement_phase(const gentest::Case &c, void *ctx, gentest::detail::BenchPhase phase, std::string &error,
                           bool &allocation_failure, bool &runtime_skipped, std::string &skip_reason,
                           gentest::detail::TestContextInfo::RuntimeSkipKind &runtime_skip_kind) {
    error.clear();
    skip_reason.clear();
    allocation_failure = false;
    runtime_skipped    = false;
    runtime_skip_kind  = gentest::detail::TestContextInfo::RuntimeSkipKind::User;
    gentest::detail::clear_bench_error();
    auto  inv     = gentest::runner::invoke_case_once(c, ctx, phase, gentest::runner::UnhandledExceptionPolicy::CaptureOnly);
    auto &ctxinfo = inv.ctxinfo;
    switch (inv.exception) {
    case gentest::runner::InvokeException::None: break;
    case gentest::runner::InvokeException::Skip: runtime_skipped = true; break;
    case gentest::runner::InvokeException::Assertion:
    case gentest::runner::InvokeException::Failure:
    case gentest::runner::InvokeException::StdException:
    case gentest::runner::InvokeException::Unknown: error = inv.message; break;
    }
    {
        std::lock_guard<std::mutex> lk(ctxinfo->mtx);
        const bool                  skip_requested = ctxinfo->runtime_skip_requested.load(std::memory_order_relaxed);
        if (skip_requested) {
            runtime_skipped   = true;
            skip_reason       = ctxinfo->runtime_skip_reason;
            runtime_skip_kind = ctxinfo->runtime_skip_kind;
        } else if (runtime_skipped) {
            runtime_skipped = false;
            error           = "skip requested without active runtime skip state";
        }
        if (!runtime_skipped && error.empty() && !ctxinfo->failures.empty()) {
            error = ctxinfo->failures.front();
        }
    }
    if (runtime_skipped)
        return false;
    if (!error.empty())
        return false;
    if (gentest::detail::has_bench_error()) {
        error              = gentest::detail::take_bench_error();
        allocation_failure = true;
        return false;
    }
    return true;
}
// NOLINTEND(bugprone-easily-swappable-parameters)

BenchResult run_bench(const gentest::Case &c, void *ctx, const BenchConfig &cfg) {
    BenchResult br{};
    auto        calibration = calibrate_epoch_iterations(c, ctx, cfg);
    auto        iters       = calibration.iterations;
    auto        done        = calibration.completed;
    auto        had_assert  = calibration.had_assert;
    const auto  calib_s     = calibration.elapsed_s;
    br.calibration_time_s   = calib_s;
    br.calibration_iters    = iters;
    if (!had_assert) {
        br.warmup_time_s = run_warmup_epochs(c, ctx, iters, cfg.warmup_epochs, done, had_assert);
    }

    std::vector<double> epoch_ns;
    if (!had_assert) {
        auto        start_all  = std::chrono::steady_clock::now();
        std::size_t epochs_run = 0;
        for (;;) {
            if (epochs_run >= cfg.measure_epochs && br.total_time_s >= cfg.min_total_time_s)
                break;
            double s = run_epoch_calls(c, ctx, iters, done, had_assert);
            if (had_assert) {
                br.total_time_s += s;
                br.total_iters += done;
                break;
            }
            const std::size_t iter_count = done ? done : 1;
            epoch_ns.push_back(ns_from_s(s) / static_cast<double>(iter_count));
            br.total_time_s += s;
            br.total_iters += done;
            ++epochs_run;
            auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_all).count();
            if (cfg.max_total_time_s > 0.0 && elapsed > cfg.max_total_time_s && br.total_time_s >= cfg.min_total_time_s)
                break;
        }
    }
    if (!epoch_ns.empty()) {
        std::vector<double> sorted = epoch_ns;
        std::ranges::sort(sorted);
        br.epochs          = sorted.size();
        br.iters_per_epoch = iters;
        br.best_ns         = sorted.front();
        br.worst_ns        = sorted.back();
        br.median_ns       = percentile_sorted(sorted, 0.5);
        br.mean_ns         = mean_of(epoch_ns);
        br.p05_ns          = percentile_sorted(sorted, 0.05);
        br.p95_ns          = percentile_sorted(sorted, 0.95);
    }
    br.wall_time_s = br.warmup_time_s + br.total_time_s + br.calibration_time_s;
    return br;
}

JitterResult run_jitter(const gentest::Case &c, void *ctx, const BenchConfig &cfg) {
    JitterResult jr{};
    auto         calibration = calibrate_epoch_iterations(c, ctx, cfg);
    std::size_t  iters       = calibration.iterations;
    bool         had_assert  = calibration.had_assert;
    std::size_t  done        = calibration.completed;
    std::size_t  epoch_count = 0;
    const double calib_s     = calibration.elapsed_s;
    jr.calibration_time_s    = calib_s;
    jr.calibration_iters     = iters;

    const std::size_t      calib_iters        = done ? done : iters;
    const double           real_ns_per_iter   = (calib_iters > 0) ? (ns_from_s(calib_s) / static_cast<double>(calib_iters)) : 0.0;
    constexpr std::size_t  kOverheadSamples   = 256;
    const OverheadEstimate per_iter_overhead  = estimate_timer_overhead_per_iter(kOverheadSamples);
    constexpr double       kOverheadThreshold = 10.0;
    const bool             use_batch          = (real_ns_per_iter > 0.0) && (per_iter_overhead.mean_ns > 0.0) &&
                           (real_ns_per_iter < per_iter_overhead.mean_ns * kOverheadThreshold);

    std::size_t      batch_samples = 1;
    std::size_t      batch_iters   = 1;
    OverheadEstimate overhead      = per_iter_overhead;
    if (use_batch) {
        batch_samples = std::min<std::size_t>(64, iters);
        if (batch_samples == 0)
            batch_samples = 1;
        batch_iters   = std::max<std::size_t>(1, iters / batch_samples);
        overhead      = estimate_timer_overhead_batch(kOverheadSamples, batch_iters);
        jr.batch_mode = true;
    }
    jr.overhead_mean_ns = overhead.mean_ns;
    jr.overhead_sd_ns   = overhead.stddev_ns;

    if (!had_assert) {
        jr.warmup_time_s = run_warmup_epochs(c, ctx, iters, cfg.warmup_epochs, done, had_assert);
    }
    if (!had_assert) {
        auto start_all = std::chrono::steady_clock::now();
        for (;;) {
            if (epoch_count >= cfg.measure_epochs && jr.total_time_s >= cfg.min_total_time_s)
                break;
            double s = 0.0;
            if (use_batch) {
                s = run_jitter_batch_epoch_calls(c, ctx, batch_iters, batch_samples, done, had_assert, jr.samples_ns);
            } else {
                s = run_jitter_epoch_calls(c, ctx, iters, done, had_assert, jr.samples_ns);
            }
            if (had_assert) {
                jr.total_time_s += s;
                jr.total_iters += done;
                break;
            }
            ++epoch_count;
            jr.total_time_s += s;
            jr.total_iters += done;
            auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_all).count();
            if (cfg.max_total_time_s > 0.0 && elapsed > cfg.max_total_time_s && jr.total_time_s >= cfg.min_total_time_s)
                break;
        }
    }
    jr.epochs          = epoch_count;
    jr.iters_per_epoch = use_batch ? (batch_iters * batch_samples) : iters;
    if (!jr.samples_ns.empty()) {
        const auto stats = gentest::detail::compute_sample_stats(jr.samples_ns);
        jr.min_ns        = stats.min;
        jr.max_ns        = stats.max;
        jr.median_ns     = stats.median;
        jr.mean_ns       = stats.mean;
        jr.stddev_ns     = stats.stddev;
        jr.p05_ns        = stats.p05;
        jr.p95_ns        = stats.p95;
    }
    if (jr.median_ns > 0.0) {
        jr.overhead_ratio_pct = (jr.overhead_mean_ns / jr.median_ns) * 100.0;
    }
    jr.wall_time_s = jr.warmup_time_s + jr.total_time_s + jr.calibration_time_s;
    return jr;
}

template <typename Result, typename CallFn>
bool run_measured_case(const gentest::Case &c, CallFn &&run_call, Result &out_result, MeasurementCaseFailure &out_failure) {
    void       *ctx = nullptr;
    std::string reason;
    if (!gentest::runner::acquire_case_fixture(c, ctx, reason)) {
        if (reason.empty()) {
            reason = "fixture allocation returned null";
        }
        if (!c.fixture.empty()) {
            out_failure.reason = fmt::format("shared fixture unavailable for '{}': {}", c.fixture, reason);
        } else {
            out_failure.reason = std::move(reason);
        }
        out_failure.skipped       = true;
        out_failure.infra_failure = true;
        out_failure.phase         = "allocation";
        return false;
    }

    struct MeasurementPhaseResult {
        bool                                              ok                 = false;
        bool                                              allocation_failure = false;
        bool                                              runtime_skipped    = false;
        std::string                                       reason;
        std::string                                       skip_reason;
        gentest::detail::TestContextInfo::RuntimeSkipKind skip_kind = gentest::detail::TestContextInfo::RuntimeSkipKind::User;
    };

    auto run_phase = [&](gentest::detail::BenchPhase phase) {
        MeasurementPhaseResult pr{};
        pr.ok = run_measurement_phase(c, ctx, phase, pr.reason, pr.allocation_failure, pr.runtime_skipped, pr.skip_reason, pr.skip_kind);
        return pr;
    };

    const MeasurementPhaseResult setup_phase = run_phase(gentest::detail::BenchPhase::Setup);
    if (!setup_phase.ok) {
        const MeasurementPhaseResult teardown_after_setup = run_phase(gentest::detail::BenchPhase::Teardown);
        if (!teardown_after_setup.ok) {
            if (setup_phase.runtime_skipped && teardown_after_setup.runtime_skipped) {
                const std::string setup_issue =
                    setup_phase.skip_reason.empty() ? std::string("setup requested skip") : setup_phase.skip_reason;
                const std::string teardown_issue =
                    teardown_after_setup.skip_reason.empty() ? std::string("teardown requested skip") : teardown_after_setup.skip_reason;
                out_failure.reason =
                    (setup_issue == teardown_issue) ? setup_issue : fmt::format("{}; teardown: {}", setup_issue, teardown_issue);
                out_failure.skipped = true;
                out_failure.infra_failure =
                    (setup_phase.skip_kind == gentest::detail::TestContextInfo::RuntimeSkipKind::SharedFixtureInfra) ||
                    (teardown_after_setup.skip_kind == gentest::detail::TestContextInfo::RuntimeSkipKind::SharedFixtureInfra);
                out_failure.phase = "setup+teardown";
                return false;
            }
            const std::string setup_issue =
                setup_phase.runtime_skipped
                    ? (setup_phase.skip_reason.empty() ? std::string("setup requested skip") : setup_phase.skip_reason)
                    : (setup_phase.reason.empty() ? std::string("setup failed") : setup_phase.reason);
            const std::string teardown_issue =
                teardown_after_setup.runtime_skipped
                    ? (teardown_after_setup.skip_reason.empty() ? std::string("teardown requested skip") : teardown_after_setup.skip_reason)
                    : (teardown_after_setup.reason.empty() ? std::string("teardown failed") : teardown_after_setup.reason);
            out_failure.reason             = fmt::format("setup issue: {}; teardown issue: {}", setup_issue, teardown_issue);
            out_failure.allocation_failure = setup_phase.allocation_failure || teardown_after_setup.allocation_failure;
            out_failure.skipped            = false;
            out_failure.infra_failure =
                (setup_phase.skip_kind == gentest::detail::TestContextInfo::RuntimeSkipKind::SharedFixtureInfra) ||
                (teardown_after_setup.skip_kind == gentest::detail::TestContextInfo::RuntimeSkipKind::SharedFixtureInfra);
            out_failure.phase = "setup+teardown";
            return false;
        }

        if (setup_phase.runtime_skipped) {
            out_failure.reason        = setup_phase.skip_reason;
            out_failure.skipped       = true;
            out_failure.infra_failure = (setup_phase.skip_kind == gentest::detail::TestContextInfo::RuntimeSkipKind::SharedFixtureInfra);
            out_failure.phase         = "setup";
            return false;
        }
        out_failure.reason             = setup_phase.reason;
        out_failure.allocation_failure = setup_phase.allocation_failure;
        out_failure.phase              = "setup";
        return false;
    }

    out_result = run_call(c, ctx);

    std::string call_error;
    if (gentest::detail::has_bench_error()) {
        call_error = gentest::detail::take_bench_error();
    }

    const MeasurementPhaseResult teardown_phase = run_phase(gentest::detail::BenchPhase::Teardown);
    if (!teardown_phase.ok) {
        if (!call_error.empty()) {
            const std::string teardown_issue =
                teardown_phase.runtime_skipped
                    ? (teardown_phase.skip_reason.empty() ? std::string("teardown requested skip") : teardown_phase.skip_reason)
                    : (teardown_phase.reason.empty() ? std::string("teardown failed") : teardown_phase.reason);
            out_failure.reason             = fmt::format("call issue: {}; teardown issue: {}", call_error, teardown_issue);
            out_failure.allocation_failure = teardown_phase.allocation_failure;
            out_failure.skipped            = false;
            out_failure.infra_failure = (teardown_phase.skip_kind == gentest::detail::TestContextInfo::RuntimeSkipKind::SharedFixtureInfra);
            out_failure.phase         = "call+teardown";
            return false;
        }
        if (teardown_phase.runtime_skipped) {
            out_failure.reason = teardown_phase.skip_reason.empty() ? std::string("teardown requested skip") : teardown_phase.skip_reason;
            out_failure.allocation_failure = false;
            out_failure.infra_failure = (teardown_phase.skip_kind == gentest::detail::TestContextInfo::RuntimeSkipKind::SharedFixtureInfra);
            out_failure.phase         = "teardown";
            return false;
        }
        out_failure.reason             = teardown_phase.reason;
        out_failure.allocation_failure = teardown_phase.allocation_failure;
        out_failure.phase              = "teardown";
        return false;
    }

    if (!call_error.empty()) {
        out_failure.reason             = std::move(call_error);
        out_failure.allocation_failure = false;
        out_failure.phase              = "call";
        return false;
    }

    return true;
}

std::string format_measured_fixture_failure_message(std::string_view kind_label, const gentest::Case &c, std::string_view reason,
                                                    bool allocation_failure, std::string_view phase) {
    if (allocation_failure) {
        if (!c.fixture.empty()) {
            return fmt::format("{} fixture allocation failed for {} ({}): {}", kind_label, c.name, c.fixture, reason);
        } else {
            return fmt::format("{} fixture allocation failed for {}: {}", kind_label, c.name, reason);
        }
    } else {
        if (!c.fixture.empty()) {
            return fmt::format("{} {} failed for {} ({}): {}", kind_label, phase, c.name, c.fixture, reason);
        } else {
            return fmt::format("{} {} failed for {}: {}", kind_label, phase, c.name, reason);
        }
    }
}

void report_measured_case_skip(const gentest::Case &c, std::string_view reason) {
    if (!reason.empty()) {
        fmt::print("[ SKIP ] {} :: {} (0 ms)\n", c.name, reason);
    } else {
        fmt::print("[ SKIP ] {} (0 ms)\n", c.name);
    }
}

template <typename Result, typename CallFn, typename SuccessFn>
TimedRunStatus run_measured_cases(std::span<const gentest::Case> kCases, std::span<const std::size_t> idxs, std::string_view kind_label,
                                  bool fail_fast, CallFn run_call, const SuccessFn &on_success, const MeasurementFailureFn &on_failure) {
    TimedRunStatus status{};
    for (auto i : idxs) {
        const auto            &c = kCases[i];
        Result                 result{};
        MeasurementCaseFailure failure{};
        ++status.total;
        if (!run_measured_case(c, run_call, result, failure)) {
            if (failure.skipped && !failure.infra_failure) {
                report_measured_case_skip(c, failure.reason);
                on_failure(c, failure, {});
                ++status.skipped;
                continue;
            }
            const std::string message =
                format_measured_fixture_failure_message(kind_label, c, failure.reason, failure.allocation_failure, failure.phase);
            fmt::print(stderr, "{}\n", message);
            on_failure(c, failure, message);
            status.ok = false;
            ++status.failed;
            if (fail_fast)
                return TimedRunStatus{.ok      = false,
                                      .stopped = true,
                                      .total   = status.total,
                                      .passed  = status.passed,
                                      .skipped = status.skipped,
                                      .failed  = status.failed};
            continue;
        }
        on_success(c, std::move(result));
        ++status.passed;
    }
    return status;
}

} // namespace

TimedRunStatus run_selected_benches(std::span<const gentest::Case> kCases, std::span<const std::size_t> idxs, const CliOptions &opt,
                                    bool fail_fast, const BenchSuccessFn &on_success, const MeasurementFailureFn &on_failure) {
    if (idxs.empty())
        return TimedRunStatus{};

    std::vector<BenchReportRow> rows;
    rows.reserve(idxs.size());
    const TimedRunStatus measured_status = run_measured_cases<BenchResult>(
        kCases, idxs, "benchmark", fail_fast,
        [&](const gentest::Case &measured, void *measured_ctx) { return run_bench(measured, measured_ctx, opt.bench_cfg); },
        [&](const gentest::Case &measured, BenchResult &&br) {
            on_success(measured, br);
            rows.push_back(BenchReportRow{
                .c      = &measured,
                .result = br,
            });
        },
        on_failure);
    if (measured_status.stopped)
        return measured_status;
    print_bench_report(rows, opt);
    return measured_status;
}

TimedRunStatus run_selected_jitters(std::span<const gentest::Case> kCases, std::span<const std::size_t> idxs, const CliOptions &opt,
                                    bool fail_fast, const JitterSuccessFn &on_success, const MeasurementFailureFn &on_failure) {
    if (idxs.empty())
        return TimedRunStatus{};

    std::vector<JitterReportRow> rows;
    rows.reserve(idxs.size());
    const TimedRunStatus measured_status = run_measured_cases<JitterResult>(
        kCases, idxs, "jitter", fail_fast,
        [&](const gentest::Case &measured, void *measured_ctx) { return run_jitter(measured, measured_ctx, opt.bench_cfg); },
        [&](const gentest::Case &measured, JitterResult &&jr) {
            jr.histogram_bins = opt.jitter_bins;
            jr.histogram      = gentest::detail::compute_histogram(jr.samples_ns, opt.jitter_bins);
            on_success(measured, jr);
            rows.push_back(JitterReportRow{
                .c      = &measured,
                .result = std::move(jr),
            });
        },
        on_failure);
    if (measured_status.stopped)
        return measured_status;
    print_jitter_report(rows, opt);
    return measured_status;
}

} // namespace gentest::runner
