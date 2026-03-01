#include "runner_measured_executor.h"

#include "gentest/detail/bench_stats.h"
#include "runner_case_invoker.h"
#include "runner_fixture_runtime.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fmt/format.h>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <tabulate/table.hpp>
#include <utility>
#include <vector>

namespace gentest::runner {
namespace {

inline double ns_from_s(double s) { return s * 1e9; }

enum class TimeDisplayUnit {
    Ns,
    Us,
    Ms,
    S,
};

struct TimeDisplaySpec {
    TimeDisplayUnit  unit        = TimeDisplayUnit::Ns;
    double           ns_per_unit = 1.0;
    std::string_view suffix      = "ns";
};

TimeDisplaySpec pick_time_display_spec_from_ns(double abs_ns_max, TimeUnitMode mode) {
    if (mode == TimeUnitMode::Ns)
        return TimeDisplaySpec{
            .unit        = TimeDisplayUnit::Ns,
            .ns_per_unit = 1.0,
            .suffix      = "ns",
        };
    if (abs_ns_max >= 1e9)
        return TimeDisplaySpec{
            .unit        = TimeDisplayUnit::S,
            .ns_per_unit = 1e9,
            .suffix      = "s",
        };
    if (abs_ns_max >= 1e6)
        return TimeDisplaySpec{
            .unit        = TimeDisplayUnit::Ms,
            .ns_per_unit = 1e6,
            .suffix      = "ms",
        };
    if (abs_ns_max >= 1e3)
        return TimeDisplaySpec{
            .unit        = TimeDisplayUnit::Us,
            .ns_per_unit = 1e3,
            .suffix      = "us",
        };
    return TimeDisplaySpec{
        .unit        = TimeDisplayUnit::Ns,
        .ns_per_unit = 1.0,
        .suffix      = "ns",
    };
}

TimeDisplaySpec pick_time_display_spec_from_s(double abs_s_max, TimeUnitMode mode) {
    return pick_time_display_spec_from_ns(ns_from_s(abs_s_max), mode);
}

bool pick_finer_time_display_spec(const TimeDisplaySpec &current, TimeDisplaySpec &out_spec) {
    switch (current.unit) {
    case TimeDisplayUnit::S:
        out_spec = TimeDisplaySpec{
            .unit        = TimeDisplayUnit::Ms,
            .ns_per_unit = 1e6,
            .suffix      = "ms",
        };
        return true;
    case TimeDisplayUnit::Ms:
        out_spec = TimeDisplaySpec{
            .unit        = TimeDisplayUnit::Us,
            .ns_per_unit = 1e3,
            .suffix      = "us",
        };
        return true;
    case TimeDisplayUnit::Us:
        out_spec = TimeDisplaySpec{
            .unit        = TimeDisplayUnit::Ns,
            .ns_per_unit = 1.0,
            .suffix      = "ns",
        };
        return true;
    case TimeDisplayUnit::Ns: return false;
    }
    return false;
}

std::string format_scaled_time_ns(double value_ns, const TimeDisplaySpec &spec) {
    const double scaled = value_ns / spec.ns_per_unit;
    if (spec.unit == TimeDisplayUnit::Ns) {
        const double rounded = std::round(scaled);
        if (std::fabs(rounded - scaled) < 1e-9)
            return fmt::format("{:.0f}", scaled);
        return fmt::format("{:.3f}", scaled);
    }
    return fmt::format("{:.3f}", scaled);
}

std::string format_scaled_time_s(double value_s, const TimeDisplaySpec &spec) { return format_scaled_time_ns(ns_from_s(value_s), spec); }

struct DisplayHistogramBin {
    std::string lo_text;
    std::string hi_text;
    bool        inclusive_hi = false;
    std::size_t count        = 0;
};

std::vector<DisplayHistogramBin> make_display_histogram_bins(std::span<const gentest::detail::HistogramBin> bins,
                                                             const TimeDisplaySpec                         &spec) {
    std::vector<DisplayHistogramBin> display_bins;
    display_bins.reserve(bins.size());
    for (const auto &bin : bins) {
        display_bins.push_back(DisplayHistogramBin{
            .lo_text      = format_scaled_time_ns(bin.lo, spec),
            .hi_text      = format_scaled_time_ns(bin.hi, spec),
            .inclusive_hi = bin.inclusive_hi,
            .count        = bin.count,
        });
    }
    return display_bins;
}

bool has_duplicate_display_ranges(std::span<const DisplayHistogramBin> bins) {
    if (bins.size() < 2)
        return false;
    for (std::size_t i = 1; i < bins.size(); ++i) {
        if (bins[i - 1].lo_text == bins[i].lo_text && bins[i - 1].hi_text == bins[i].hi_text)
            return true;
    }
    return false;
}

std::vector<DisplayHistogramBin> merge_duplicate_display_ranges(std::span<const DisplayHistogramBin> bins) {
    std::vector<DisplayHistogramBin> merged;
    if (bins.empty())
        return merged;

    merged.reserve(bins.size());
    merged.push_back(bins.front());
    for (std::size_t i = 1; i < bins.size(); ++i) {
        const auto &bin  = bins[i];
        auto       &last = merged.back();
        if (last.lo_text == bin.lo_text && last.hi_text == bin.hi_text) {
            last.count += bin.count;
            last.inclusive_hi = last.inclusive_hi || bin.inclusive_hi;
            continue;
        }
        merged.push_back(bin);
    }
    return merged;
}

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

double percentile_sorted(const std::vector<double> &v, double p) {
    if (v.empty())
        return 0.0;
    if (v.size() == 1)
        return v.front();
    if (p <= 0.0)
        return v.front();
    if (p >= 1.0)
        return v.back();
    const double      idx  = p * static_cast<double>(v.size() - 1);
    const std::size_t lo   = static_cast<std::size_t>(idx);
    const std::size_t hi   = (lo + 1 < v.size()) ? (lo + 1) : lo;
    const double      frac = idx - static_cast<double>(lo);
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

double run_epoch_calls(const gentest::Case &c, void *ctx, std::size_t iters, std::size_t &iterations_done, bool &had_assert_fail) {
    using clock           = std::chrono::steady_clock;
    auto ctxinfo          = std::make_shared<gentest::detail::TestContextInfo>();
    ctxinfo->display_name = std::string(c.name);
    ctxinfo->active       = true;
    gentest::detail::set_current_test(ctxinfo);
    gentest::detail::BenchPhaseScope bench_scope(gentest::detail::BenchPhase::Call);
    auto                             start = clock::now();
    had_assert_fail                        = false;
    iterations_done                        = 0;
    try {
        for (std::size_t i = 0; i < iters; ++i) {
            c.fn(ctx);
            iterations_done = i + 1;
        }
    } catch (const gentest::detail::skip_exception &) {
        record_runtime_skip_or_default(ctxinfo, "skip requested during benchmark call phase");
        had_assert_fail = true;
    } catch (const gentest::assertion &e) {
        gentest::detail::record_bench_error(e.message());
        had_assert_fail = true;
    } catch (const gentest::failure &e) {
        gentest::detail::record_bench_error(e.what());
        had_assert_fail = true;
    } catch (const std::exception &e) {
        gentest::detail::record_bench_error(std::string("std::exception: ") + e.what());
        had_assert_fail = true;
    } catch (...) {
        gentest::detail::record_bench_error("unknown exception");
        had_assert_fail = true;
    }
    finalize_call_phase_failure(ctxinfo, "skip requested during benchmark call phase", had_assert_fail);
    auto end        = clock::now();
    ctxinfo->active = false;
    gentest::detail::set_current_test(nullptr);
    return std::chrono::duration<double>(end - start).count();
}

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

double run_jitter_epoch_calls(const gentest::Case &c, void *ctx, std::size_t iters, std::size_t &iterations_done, bool &had_assert_fail,
                              std::vector<double> &samples_ns) {
    using clock           = std::chrono::steady_clock;
    auto ctxinfo          = std::make_shared<gentest::detail::TestContextInfo>();
    ctxinfo->display_name = std::string(c.name);
    ctxinfo->active       = true;
    gentest::detail::set_current_test(ctxinfo);
    gentest::detail::BenchPhaseScope bench_scope(gentest::detail::BenchPhase::Call);
    auto                             epoch_start = clock::now();
    had_assert_fail                              = false;
    iterations_done                              = 0;
    try {
        for (std::size_t i = 0; i < iters; ++i) {
            auto start = clock::now();
            c.fn(ctx);
            auto end = clock::now();
            samples_ns.push_back(ns_from_s(std::chrono::duration<double>(end - start).count()));
            iterations_done = i + 1;
        }
    } catch (const gentest::detail::skip_exception &) {
        record_runtime_skip_or_default(ctxinfo, "skip requested during jitter call phase");
        had_assert_fail = true;
    } catch (const gentest::assertion &e) {
        gentest::detail::record_bench_error(e.message());
        had_assert_fail = true;
    } catch (const gentest::failure &e) {
        gentest::detail::record_bench_error(e.what());
        had_assert_fail = true;
    } catch (const std::exception &e) {
        gentest::detail::record_bench_error(std::string("std::exception: ") + e.what());
        had_assert_fail = true;
    } catch (...) {
        gentest::detail::record_bench_error("unknown exception");
        had_assert_fail = true;
    }
    finalize_call_phase_failure(ctxinfo, "skip requested during jitter call phase", had_assert_fail);
    auto epoch_end  = clock::now();
    ctxinfo->active = false;
    gentest::detail::set_current_test(nullptr);
    return std::chrono::duration<double>(epoch_end - epoch_start).count();
}

double run_jitter_batch_epoch_calls(const gentest::Case &c, void *ctx, std::size_t batch_iters, std::size_t batch_samples,
                                    std::size_t &iterations_done, bool &had_assert_fail, std::vector<double> &samples_ns) {
    using clock           = std::chrono::steady_clock;
    auto ctxinfo          = std::make_shared<gentest::detail::TestContextInfo>();
    ctxinfo->display_name = std::string(c.name);
    ctxinfo->active       = true;
    gentest::detail::set_current_test(ctxinfo);
    gentest::detail::BenchPhaseScope bench_scope(gentest::detail::BenchPhase::Call);
    auto                             epoch_start = clock::now();
    had_assert_fail                              = false;
    iterations_done                              = 0;
    std::size_t local_done                       = 0;
    auto        batch_start                      = clock::now();
    bool        in_batch                         = false;
    try {
        for (std::size_t s = 0; s < batch_samples; ++s) {
            batch_start = clock::now();
            local_done  = 0;
            in_batch    = true;
            for (std::size_t i = 0; i < batch_iters; ++i) {
                c.fn(ctx);
                ++local_done;
            }
            auto end = clock::now();
            if (local_done != 0) {
                samples_ns.push_back(ns_from_s(std::chrono::duration<double>(end - batch_start).count()) / static_cast<double>(local_done));
                iterations_done += local_done;
            }
            in_batch = false;
        }
    } catch (const gentest::detail::skip_exception &) {
        if (in_batch && local_done != 0) {
            auto end = clock::now();
            samples_ns.push_back(ns_from_s(std::chrono::duration<double>(end - batch_start).count()) / static_cast<double>(local_done));
            iterations_done += local_done;
        }
        record_runtime_skip_or_default(ctxinfo, "skip requested during jitter call phase");
        had_assert_fail = true;
    } catch (const gentest::assertion &e) {
        if (in_batch && local_done != 0) {
            auto end = clock::now();
            samples_ns.push_back(ns_from_s(std::chrono::duration<double>(end - batch_start).count()) / static_cast<double>(local_done));
            iterations_done += local_done;
        }
        gentest::detail::record_bench_error(e.message());
        had_assert_fail = true;
    } catch (const gentest::failure &e) {
        if (in_batch && local_done != 0) {
            auto end = clock::now();
            samples_ns.push_back(ns_from_s(std::chrono::duration<double>(end - batch_start).count()) / static_cast<double>(local_done));
            iterations_done += local_done;
        }
        gentest::detail::record_bench_error(e.what());
        had_assert_fail = true;
    } catch (const std::exception &e) {
        if (in_batch && local_done != 0) {
            auto end = clock::now();
            samples_ns.push_back(ns_from_s(std::chrono::duration<double>(end - batch_start).count()) / static_cast<double>(local_done));
            iterations_done += local_done;
        }
        gentest::detail::record_bench_error(std::string("std::exception: ") + e.what());
        had_assert_fail = true;
    } catch (...) {
        if (in_batch && local_done != 0) {
            auto end = clock::now();
            samples_ns.push_back(ns_from_s(std::chrono::duration<double>(end - batch_start).count()) / static_cast<double>(local_done));
            iterations_done += local_done;
        }
        gentest::detail::record_bench_error("unknown exception");
        had_assert_fail = true;
    }
    finalize_call_phase_failure(ctxinfo, "skip requested during jitter call phase", had_assert_fail);
    auto epoch_end  = clock::now();
    ctxinfo->active = false;
    gentest::detail::set_current_test(nullptr);
    return std::chrono::duration<double>(epoch_end - epoch_start).count();
}

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

BenchResult run_bench(const gentest::Case &c, void *ctx, const BenchConfig &cfg) {
    BenchResult br{};
    std::size_t iters      = 1;
    bool        had_assert = false;
    std::size_t done       = 0;
    double      calib_s    = 0.0;
    while (true) {
        calib_s = run_epoch_calls(c, ctx, iters, done, had_assert);
        if (had_assert)
            break;
        if (calib_s >= cfg.min_epoch_time_s)
            break;
        iters *= 2;
        if (iters == 0 || iters > (std::size_t(1) << 30))
            break;
    }
    br.calibration_time_s = calib_s;
    br.calibration_iters  = iters;

    for (std::size_t i = 0; i < cfg.warmup_epochs; ++i) {
        br.warmup_time_s += run_epoch_calls(c, ctx, iters, done, had_assert);
        if (had_assert)
            break;
    }

    std::vector<double> epoch_ns;
    auto                start_all  = std::chrono::steady_clock::now();
    std::size_t         epochs_run = 0;
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
    if (!epoch_ns.empty()) {
        std::vector<double> sorted = epoch_ns;
        std::sort(sorted.begin(), sorted.end());
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
    std::size_t  iters       = 1;
    bool         had_assert  = false;
    std::size_t  done        = 0;
    std::size_t  epoch_count = 0;
    double       calib_s     = 0.0;
    while (true) {
        calib_s = run_epoch_calls(c, ctx, iters, done, had_assert);
        if (had_assert)
            break;
        if (calib_s >= cfg.min_epoch_time_s)
            break;
        iters *= 2;
        if (iters == 0 || iters > (std::size_t(1) << 30))
            break;
    }
    jr.calibration_time_s = calib_s;
    jr.calibration_iters  = iters;

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

    for (std::size_t i = 0; i < cfg.warmup_epochs; ++i) {
        jr.warmup_time_s += run_epoch_calls(c, ctx, iters, done, had_assert);
        if (had_assert)
            break;
    }
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

    bool        allocation_failure = false;
    bool        runtime_skipped    = false;
    std::string skip_reason;
    auto        runtime_skip_kind = gentest::detail::TestContextInfo::RuntimeSkipKind::User;
    if (!run_measurement_phase(c, ctx, gentest::detail::BenchPhase::Setup, reason, allocation_failure, runtime_skipped, skip_reason,
                               runtime_skip_kind)) {
        if (runtime_skipped) {
            out_failure.reason        = std::move(skip_reason);
            out_failure.skipped       = true;
            out_failure.infra_failure = (runtime_skip_kind == gentest::detail::TestContextInfo::RuntimeSkipKind::SharedFixtureInfra);
            out_failure.phase         = "setup";
            return false;
        }
        out_failure.reason             = std::move(reason);
        out_failure.allocation_failure = allocation_failure;
        out_failure.phase              = "setup";
        return false;
    }

    out_result = run_call(c, ctx);

    std::string call_error;
    if (gentest::detail::has_bench_error()) {
        call_error = gentest::detail::take_bench_error();
    }

    if (!run_measurement_phase(c, ctx, gentest::detail::BenchPhase::Teardown, reason, allocation_failure, runtime_skipped, skip_reason,
                               runtime_skip_kind)) {
        if (runtime_skipped) {
            out_failure.reason             = skip_reason.empty() ? std::string("teardown requested skip") : std::move(skip_reason);
            out_failure.allocation_failure = false;
            out_failure.infra_failure      = (runtime_skip_kind == gentest::detail::TestContextInfo::RuntimeSkipKind::SharedFixtureInfra);
            out_failure.phase              = "teardown";
            return false;
        }
        out_failure.reason             = std::move(reason);
        out_failure.allocation_failure = allocation_failure;
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
    bool had_fixture_failure = false;
    for (auto i : idxs) {
        const auto            &c = kCases[i];
        Result                 result{};
        MeasurementCaseFailure failure{};
        if (!run_measured_case(c, run_call, result, failure)) {
            if (failure.skipped) {
                report_measured_case_skip(c, failure.reason);
                on_failure(c, failure, {});
                if (failure.infra_failure) {
                    had_fixture_failure = true;
                    if (fail_fast)
                        return TimedRunStatus{false, true};
                }
                continue;
            }
            const std::string message =
                format_measured_fixture_failure_message(kind_label, c, failure.reason, failure.allocation_failure, failure.phase);
            fmt::print(stderr, "{}\n", message);
            on_failure(c, failure, message);
            had_fixture_failure = true;
            if (fail_fast)
                return TimedRunStatus{false, true};
            continue;
        }
        on_success(c, std::move(result));
    }
    return TimedRunStatus{!had_fixture_failure};
}

} // namespace

TimedRunStatus run_selected_benches(std::span<const gentest::Case> kCases, std::span<const std::size_t> idxs, const CliOptions &opt,
                                    bool fail_fast, const BenchSuccessFn &on_success, const MeasurementFailureFn &on_failure) {
    if (idxs.empty())
        return TimedRunStatus{};

    struct BenchRow {
        const gentest::Case *c = nullptr;
        BenchResult          br{};
    };
    std::vector<BenchRow> rows;
    rows.reserve(idxs.size());
    const TimedRunStatus measured_status = run_measured_cases<BenchResult>(
        kCases, idxs, "benchmark", fail_fast,
        [&](const gentest::Case &measured, void *measured_ctx) { return run_bench(measured, measured_ctx, opt.bench_cfg); },
        [&](const gentest::Case &measured, BenchResult &&br) {
            on_success(measured, br);
            rows.push_back(BenchRow{
                .c  = &measured,
                .br = std::move(br),
            });
        },
        on_failure);
    if (measured_status.stopped)
        return measured_status;

    std::map<std::string, double> baseline_ns;
    for (const auto &row : rows) {
        if (!row.c || !row.c->is_baseline)
            continue;
        const std::string suite(row.c->suite);
        if (baseline_ns.find(suite) == baseline_ns.end()) {
            baseline_ns.emplace(suite, row.br.median_ns);
        }
    }

    using tabulate::FontAlign;
    using tabulate::Table;
    using Row_t = Table::Row_t;

    const auto bench_calls_per_sec = [](const BenchResult &br) -> double {
        if (br.total_time_s <= 0.0 || br.total_iters == 0)
            return 0.0;
        return static_cast<double>(br.total_iters) / br.total_time_s;
    };

    const auto bench_max_abs_ns = [&](const auto &projector) {
        double max_abs = 0.0;
        for (const auto &row : rows) {
            if (!row.c)
                continue;
            max_abs = std::max(max_abs, std::fabs(projector(row.br)));
        }
        return max_abs;
    };
    const auto bench_max_abs_s = [&](const auto &projector) {
        double max_abs = 0.0;
        for (const auto &row : rows) {
            if (!row.c)
                continue;
            max_abs = std::max(max_abs, std::fabs(projector(row.br)));
        }
        return max_abs;
    };

    const TimeDisplaySpec median_spec =
        pick_time_display_spec_from_ns(bench_max_abs_ns([](const BenchResult &br) { return br.median_ns; }), opt.time_unit_mode);
    const TimeDisplaySpec mean_spec =
        pick_time_display_spec_from_ns(bench_max_abs_ns([](const BenchResult &br) { return br.mean_ns; }), opt.time_unit_mode);
    const TimeDisplaySpec p05_spec =
        pick_time_display_spec_from_ns(bench_max_abs_ns([](const BenchResult &br) { return br.p05_ns; }), opt.time_unit_mode);
    const TimeDisplaySpec p95_spec =
        pick_time_display_spec_from_ns(bench_max_abs_ns([](const BenchResult &br) { return br.p95_ns; }), opt.time_unit_mode);
    const TimeDisplaySpec worst_spec =
        pick_time_display_spec_from_ns(bench_max_abs_ns([](const BenchResult &br) { return br.worst_ns; }), opt.time_unit_mode);
    const TimeDisplaySpec total_spec =
        pick_time_display_spec_from_s(bench_max_abs_s([](const BenchResult &br) { return br.wall_time_s; }), opt.time_unit_mode);

    const TimeDisplaySpec measured_debug_spec =
        pick_time_display_spec_from_s(bench_max_abs_s([](const BenchResult &br) { return br.total_time_s; }), opt.time_unit_mode);
    const TimeDisplaySpec wall_debug_spec =
        pick_time_display_spec_from_s(bench_max_abs_s([](const BenchResult &br) { return br.wall_time_s; }), opt.time_unit_mode);
    const TimeDisplaySpec warmup_debug_spec =
        pick_time_display_spec_from_s(bench_max_abs_s([](const BenchResult &br) { return br.warmup_time_s; }), opt.time_unit_mode);
    const TimeDisplaySpec calib_debug_spec =
        pick_time_display_spec_from_s(bench_max_abs_s([](const BenchResult &br) { return br.calibration_time_s; }), opt.time_unit_mode);
    const TimeDisplaySpec min_epoch_debug_spec =
        pick_time_display_spec_from_s(std::fabs(opt.bench_cfg.min_epoch_time_s), opt.time_unit_mode);
    const TimeDisplaySpec min_total_debug_spec =
        pick_time_display_spec_from_s(std::fabs(opt.bench_cfg.min_total_time_s), opt.time_unit_mode);
    const TimeDisplaySpec max_total_debug_spec =
        pick_time_display_spec_from_s(std::fabs(opt.bench_cfg.max_total_time_s), opt.time_unit_mode);

    Table summary;
    summary.add_row(Row_t{"Benchmark", "Samples", "Iters/epoch", fmt::format("Median ({}/op)", median_spec.suffix),
                          fmt::format("Mean ({}/op)", mean_spec.suffix), fmt::format("P05 ({}/op)", p05_spec.suffix),
                          fmt::format("P95 ({}/op)", p95_spec.suffix), fmt::format("Worst ({}/op)", worst_spec.suffix),
                          fmt::format("Total ({})", total_spec.suffix), "Baseline Δ%"});
    summary[0].format().font_align(FontAlign::center);
    summary.column(1).format().font_align(FontAlign::right);
    summary.column(2).format().font_align(FontAlign::right);
    summary.column(3).format().font_align(FontAlign::right);
    summary.column(4).format().font_align(FontAlign::right);
    summary.column(5).format().font_align(FontAlign::right);
    summary.column(6).format().font_align(FontAlign::right);
    summary.column(7).format().font_align(FontAlign::right);
    summary.column(8).format().font_align(FontAlign::right);
    summary.column(9).format().font_align(FontAlign::right);

    for (const auto &row : rows) {
        if (!row.c)
            continue;
        const std::string suite(row.c->suite);
        const auto        base_it = baseline_ns.find(suite);
        const double      base_ns = (base_it == baseline_ns.end()) ? 0.0 : base_it->second;
        const std::string baseline_cell =
            (base_ns > 0.0) ? fmt::format("{:+.2f}%", (row.br.median_ns - base_ns) / base_ns * 100.0) : std::string("-");
        summary.add_row(Row_t{
            std::string(row.c->name),
            fmt::format("{}", row.br.epochs),
            fmt::format("{}", row.br.iters_per_epoch),
            format_scaled_time_ns(row.br.median_ns, median_spec),
            format_scaled_time_ns(row.br.mean_ns, mean_spec),
            format_scaled_time_ns(row.br.p05_ns, p05_spec),
            format_scaled_time_ns(row.br.p95_ns, p95_spec),
            format_scaled_time_ns(row.br.worst_ns, worst_spec),
            format_scaled_time_s(row.br.wall_time_s, total_spec),
            baseline_cell,
        });
    }

    Table debug;
    debug.add_row(Row_t{"Benchmark", "Epochs", "Iters/epoch", "Total iters", fmt::format("Measured ({})", measured_debug_spec.suffix),
                        fmt::format("Wall ({})", wall_debug_spec.suffix), fmt::format("Warmup ({})", warmup_debug_spec.suffix),
                        "Calib iters", fmt::format("Calib ({})", calib_debug_spec.suffix),
                        fmt::format("Min epoch ({})", min_epoch_debug_spec.suffix),
                        fmt::format("Min total ({})", min_total_debug_spec.suffix),
                        fmt::format("Max total ({})", max_total_debug_spec.suffix), "Calls/sec"});
    debug[0].format().font_align(FontAlign::center);
    for (std::size_t col = 1; col < 13; ++col) {
        debug.column(col).format().font_align(FontAlign::right);
    }

    for (const auto &row : rows) {
        if (!row.c)
            continue;
        debug.add_row(Row_t{
            std::string(row.c->name),
            fmt::format("{}", row.br.epochs),
            fmt::format("{}", row.br.iters_per_epoch),
            fmt::format("{}", row.br.total_iters),
            format_scaled_time_s(row.br.total_time_s, measured_debug_spec),
            format_scaled_time_s(row.br.wall_time_s, wall_debug_spec),
            format_scaled_time_s(row.br.warmup_time_s, warmup_debug_spec),
            fmt::format("{}", row.br.calibration_iters),
            format_scaled_time_s(row.br.calibration_time_s, calib_debug_spec),
            format_scaled_time_s(opt.bench_cfg.min_epoch_time_s, min_epoch_debug_spec),
            format_scaled_time_s(opt.bench_cfg.min_total_time_s, min_total_debug_spec),
            format_scaled_time_s(opt.bench_cfg.max_total_time_s, max_total_debug_spec),
            fmt::format("{:.3f}", bench_calls_per_sec(row.br)),
        });
    }

    std::cout << "Benchmarks\n" << summary << "\n\n";
    std::cout << "Bench debug\n" << debug << "\n";
    return TimedRunStatus{measured_status.ok};
}

TimedRunStatus run_selected_jitters(std::span<const gentest::Case> kCases, std::span<const std::size_t> idxs, const CliOptions &opt,
                                    bool fail_fast, const JitterSuccessFn &on_success, const MeasurementFailureFn &on_failure) {
    if (idxs.empty())
        return TimedRunStatus{};

    const int bins = opt.jitter_bins;
    struct JitterRow {
        const gentest::Case *c = nullptr;
        JitterResult         jr;
    };
    std::vector<JitterRow> rows;
    rows.reserve(idxs.size());
    const TimedRunStatus measured_status = run_measured_cases<JitterResult>(
        kCases, idxs, "jitter", fail_fast,
        [&](const gentest::Case &measured, void *measured_ctx) { return run_jitter(measured, measured_ctx, opt.bench_cfg); },
        [&](const gentest::Case &measured, JitterResult &&jr) {
            on_success(measured, jr);
            rows.push_back(JitterRow{
                .c  = &measured,
                .jr = std::move(jr),
            });
        },
        on_failure);
    if (measured_status.stopped)
        return measured_status;

    std::map<std::string, double> baseline_median_ns;
    std::map<std::string, double> baseline_stddev_ns;
    for (const auto &row : rows) {
        if (!row.c || !row.c->is_baseline)
            continue;
        const std::string suite(row.c->suite);
        if (baseline_median_ns.find(suite) == baseline_median_ns.end()) {
            baseline_median_ns.emplace(suite, row.jr.median_ns);
            baseline_stddev_ns.emplace(suite, row.jr.stddev_ns);
        }
    }

    using tabulate::FontAlign;
    using tabulate::Table;
    using Row_t = Table::Row_t;

    const auto jitter_max_abs_ns = [&](const auto &projector) {
        double max_abs = 0.0;
        for (const auto &row : rows) {
            if (!row.c)
                continue;
            max_abs = std::max(max_abs, std::fabs(projector(row.jr)));
        }
        return max_abs;
    };
    const auto jitter_max_abs_s = [&](const auto &projector) {
        double max_abs = 0.0;
        for (const auto &row : rows) {
            if (!row.c)
                continue;
            max_abs = std::max(max_abs, std::fabs(projector(row.jr)));
        }
        return max_abs;
    };

    const TimeDisplaySpec median_spec =
        pick_time_display_spec_from_ns(jitter_max_abs_ns([](const JitterResult &jr) { return jr.median_ns; }), opt.time_unit_mode);
    const TimeDisplaySpec mean_spec =
        pick_time_display_spec_from_ns(jitter_max_abs_ns([](const JitterResult &jr) { return jr.mean_ns; }), opt.time_unit_mode);
    const TimeDisplaySpec stddev_spec =
        pick_time_display_spec_from_ns(jitter_max_abs_ns([](const JitterResult &jr) { return jr.stddev_ns; }), opt.time_unit_mode);
    const TimeDisplaySpec p05_spec =
        pick_time_display_spec_from_ns(jitter_max_abs_ns([](const JitterResult &jr) { return jr.p05_ns; }), opt.time_unit_mode);
    const TimeDisplaySpec p95_spec =
        pick_time_display_spec_from_ns(jitter_max_abs_ns([](const JitterResult &jr) { return jr.p95_ns; }), opt.time_unit_mode);
    const TimeDisplaySpec min_spec =
        pick_time_display_spec_from_ns(jitter_max_abs_ns([](const JitterResult &jr) { return jr.min_ns; }), opt.time_unit_mode);
    const TimeDisplaySpec max_spec =
        pick_time_display_spec_from_ns(jitter_max_abs_ns([](const JitterResult &jr) { return jr.max_ns; }), opt.time_unit_mode);
    const TimeDisplaySpec total_spec =
        pick_time_display_spec_from_s(jitter_max_abs_s([](const JitterResult &jr) { return jr.wall_time_s; }), opt.time_unit_mode);

    double overhead_abs_max_ns = 0.0;
    for (const auto &row : rows) {
        if (!row.c)
            continue;
        overhead_abs_max_ns = std::max(overhead_abs_max_ns, std::fabs(row.jr.overhead_mean_ns));
        overhead_abs_max_ns = std::max(overhead_abs_max_ns, std::fabs(row.jr.overhead_sd_ns));
    }
    const TimeDisplaySpec overhead_spec = pick_time_display_spec_from_ns(overhead_abs_max_ns, opt.time_unit_mode);
    const TimeDisplaySpec measured_debug_spec =
        pick_time_display_spec_from_s(jitter_max_abs_s([](const JitterResult &jr) { return jr.total_time_s; }), opt.time_unit_mode);
    const TimeDisplaySpec warmup_debug_spec =
        pick_time_display_spec_from_s(jitter_max_abs_s([](const JitterResult &jr) { return jr.warmup_time_s; }), opt.time_unit_mode);
    const TimeDisplaySpec wall_debug_spec =
        pick_time_display_spec_from_s(jitter_max_abs_s([](const JitterResult &jr) { return jr.wall_time_s; }), opt.time_unit_mode);
    const TimeDisplaySpec min_total_debug_spec =
        pick_time_display_spec_from_s(std::fabs(opt.bench_cfg.min_total_time_s), opt.time_unit_mode);
    const TimeDisplaySpec max_total_debug_spec =
        pick_time_display_spec_from_s(std::fabs(opt.bench_cfg.max_total_time_s), opt.time_unit_mode);

    Table summary;
    summary.add_row(Row_t{"Benchmark", "Samples", fmt::format("Median ({}/op)", median_spec.suffix),
                          fmt::format("Mean ({}/op)", mean_spec.suffix), fmt::format("StdDev ({}/op)", stddev_spec.suffix),
                          fmt::format("P05 ({}/op)", p05_spec.suffix), fmt::format("P95 ({}/op)", p95_spec.suffix),
                          fmt::format("Min ({}/op)", min_spec.suffix), fmt::format("Max ({}/op)", max_spec.suffix),
                          fmt::format("Total ({})", total_spec.suffix), "Baseline Δ%", "Baseline SD Δ%"});
    summary[0].format().font_align(FontAlign::center);
    for (std::size_t col = 1; col < 12; ++col) {
        summary.column(col).format().font_align(FontAlign::right);
    }

    for (const auto &row : rows) {
        if (!row.c)
            continue;
        const std::string suite(row.c->suite);
        const auto        base_med_it = baseline_median_ns.find(suite);
        const auto        base_sd_it  = baseline_stddev_ns.find(suite);
        const double      base_median = (base_med_it == baseline_median_ns.end()) ? 0.0 : base_med_it->second;
        const double      base_sd     = (base_sd_it == baseline_stddev_ns.end()) ? 0.0 : base_sd_it->second;
        const std::string baseline_med_cell =
            (base_median > 0.0) ? fmt::format("{:+.2f}%", (row.jr.median_ns - base_median) / base_median * 100.0) : std::string("-");
        const std::string baseline_sd_cell =
            (base_sd > 0.0) ? fmt::format("{:+.2f}%", (row.jr.stddev_ns - base_sd) / base_sd * 100.0) : std::string("-");
        summary.add_row(Row_t{
            std::string(row.c->name),
            fmt::format("{}", row.jr.samples_ns.size()),
            format_scaled_time_ns(row.jr.median_ns, median_spec),
            format_scaled_time_ns(row.jr.mean_ns, mean_spec),
            format_scaled_time_ns(row.jr.stddev_ns, stddev_spec),
            format_scaled_time_ns(row.jr.p05_ns, p05_spec),
            format_scaled_time_ns(row.jr.p95_ns, p95_spec),
            format_scaled_time_ns(row.jr.min_ns, min_spec),
            format_scaled_time_ns(row.jr.max_ns, max_spec),
            format_scaled_time_s(row.jr.wall_time_s, total_spec),
            baseline_med_cell,
            baseline_sd_cell,
        });
    }

    std::cout << "Jitter summary\n" << summary << "\n";

    Table debug;
    debug.add_row(Row_t{"Benchmark", "Mode", "Samples", "Iters/epoch", fmt::format("Overhead ({}/iter)", overhead_spec.suffix),
                        "Overhead %", fmt::format("Measured ({})", measured_debug_spec.suffix),
                        fmt::format("Warmup ({})", warmup_debug_spec.suffix), fmt::format("Min total ({})", min_total_debug_spec.suffix),
                        fmt::format("Max total ({})", max_total_debug_spec.suffix), fmt::format("Wall ({})", wall_debug_spec.suffix)});
    debug[0].format().font_align(FontAlign::center);
    for (std::size_t col = 2; col < 11; ++col) {
        debug.column(col).format().font_align(FontAlign::right);
    }

    for (const auto &row : rows) {
        if (!row.c)
            continue;
        const std::string mode          = row.jr.batch_mode ? "batch" : "per-iter";
        const std::string overhead_cell = (row.jr.overhead_mean_ns > 0.0)
                                              ? fmt::format("{} ± {}", format_scaled_time_ns(row.jr.overhead_mean_ns, overhead_spec),
                                                            format_scaled_time_ns(row.jr.overhead_sd_ns, overhead_spec))
                                              : std::string("-");
        const std::string overhead_pct =
            (row.jr.overhead_ratio_pct > 0.0) ? fmt::format("{:.2f}%", row.jr.overhead_ratio_pct) : std::string("-");
        debug.add_row(Row_t{
            std::string(row.c->name),
            mode,
            fmt::format("{}", row.jr.samples_ns.size()),
            fmt::format("{}", row.jr.iters_per_epoch),
            overhead_cell,
            overhead_pct,
            format_scaled_time_s(row.jr.total_time_s, measured_debug_spec),
            format_scaled_time_s(row.jr.warmup_time_s, warmup_debug_spec),
            format_scaled_time_s(opt.bench_cfg.min_total_time_s, min_total_debug_spec),
            format_scaled_time_s(opt.bench_cfg.max_total_time_s, max_total_debug_spec),
            format_scaled_time_s(row.jr.wall_time_s, wall_debug_spec),
        });
    }

    std::cout << "Jitter debug\n" << debug << "\n";

    for (const auto &row : rows) {
        if (!row.c)
            continue;
        const auto &samples = row.jr.samples_ns;
        std::cout << "\nJitter histogram (bins=" << bins << ", name=" << row.c->name << ")\n";
        const auto hist_data = gentest::detail::compute_histogram(samples, bins);

        double hist_abs_max_ns = 0.0;
        for (double sample_ns : samples) {
            hist_abs_max_ns = std::max(hist_abs_max_ns, std::fabs(sample_ns));
        }
        TimeDisplaySpec                  hist_spec    = pick_time_display_spec_from_ns(hist_abs_max_ns, opt.time_unit_mode);
        std::vector<DisplayHistogramBin> display_bins = make_display_histogram_bins(hist_data.bins, hist_spec);
        if (opt.time_unit_mode == TimeUnitMode::Auto) {
            while (has_duplicate_display_ranges(display_bins)) {
                TimeDisplaySpec finer_spec;
                if (!pick_finer_time_display_spec(hist_spec, finer_spec))
                    break;
                hist_spec    = finer_spec;
                display_bins = make_display_histogram_bins(hist_data.bins, hist_spec);
            }
        }
        const std::size_t pre_merge_bins = display_bins.size();
        if (has_duplicate_display_ranges(display_bins)) {
            display_bins = merge_duplicate_display_ranges(display_bins);
        }
        if (display_bins.size() < pre_merge_bins) {
            fmt::print("note: merged {} histogram bins due displayed {} range precision\n", pre_merge_bins - display_bins.size(),
                       hist_spec.suffix);
        }

        Table hist;
        hist.add_row(Row_t{"Bin", fmt::format("Range ({}/op)", hist_spec.suffix), "Count", "Percent", "Cumulative %"});
        hist[0].format().font_align(FontAlign::center);
        hist.column(0).format().font_align(FontAlign::right);
        hist.column(2).format().font_align(FontAlign::right);
        hist.column(3).format().font_align(FontAlign::right);
        hist.column(4).format().font_align(FontAlign::right);

        if (samples.empty()) {
            std::cout << hist << "\n";
            continue;
        }

        const auto  total_samples    = static_cast<double>(samples.size());
        std::size_t cumulative_count = 0;
        for (std::size_t i = 0; i < display_bins.size(); ++i) {
            const auto       &bin = display_bins[i];
            const std::string range =
                bin.inclusive_hi ? fmt::format("[{}, {}]", bin.lo_text, bin.hi_text) : fmt::format("[{}, {})", bin.lo_text, bin.hi_text);
            cumulative_count += bin.count;
            const double pct            = (total_samples > 0.0) ? (static_cast<double>(bin.count) / total_samples * 100.0) : 0.0;
            const double cumulative_pct = (total_samples > 0.0) ? (static_cast<double>(cumulative_count) / total_samples * 100.0) : 0.0;
            hist.add_row(Row_t{
                fmt::format("{}", i + 1),
                range,
                fmt::format("{}", bin.count),
                fmt::format("{:.2f}%", pct),
                fmt::format("{:.2f}%", cumulative_pct),
            });
        }

        std::cout << hist << "\n";
    }
    return TimedRunStatus{measured_status.ok};
}

} // namespace gentest::runner
