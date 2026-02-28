#pragma once

#include "gentest/runner.h"
#include "runner_cli.h"

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gentest::runner {

struct BenchResult {
    std::size_t epochs             = 0;
    std::size_t iters_per_epoch    = 0;
    std::size_t total_iters        = 0;
    double      best_ns            = 0;
    double      worst_ns           = 0;
    double      median_ns          = 0;
    double      mean_ns            = 0;
    double      p05_ns             = 0;
    double      p95_ns             = 0;
    double      total_time_s       = 0;
    double      warmup_time_s      = 0;
    double      wall_time_s        = 0;
    double      calibration_time_s = 0;
    std::size_t calibration_iters  = 0;
};

struct JitterResult {
    std::size_t         epochs             = 0;
    std::size_t         iters_per_epoch    = 0;
    std::size_t         total_iters        = 0;
    bool                batch_mode         = false;
    double              min_ns             = 0;
    double              max_ns             = 0;
    double              median_ns          = 0;
    double              mean_ns            = 0;
    double              stddev_ns          = 0;
    double              p05_ns             = 0;
    double              p95_ns             = 0;
    double              overhead_mean_ns   = 0;
    double              overhead_sd_ns     = 0;
    double              overhead_ratio_pct = 0;
    double              total_time_s       = 0;
    double              warmup_time_s      = 0;
    double              wall_time_s        = 0;
    double              calibration_time_s = 0;
    std::size_t         calibration_iters  = 0;
    std::vector<double> samples_ns;
};

struct TimedRunStatus {
    bool ok      = true;
    bool stopped = false;
};

struct MeasurementCaseFailure {
    std::string      reason;
    bool             allocation_failure = false;
    bool             skipped            = false;
    bool             infra_failure      = false;
    std::string_view phase{};
};

using MeasurementFailureFn = std::function<void(const gentest::Case &, const MeasurementCaseFailure &, std::string_view)>;
using BenchSuccessFn       = std::function<void(const gentest::Case &, const BenchResult &)>;
using JitterSuccessFn      = std::function<void(const gentest::Case &, const JitterResult &)>;

bool acquire_case_fixture(const gentest::Case &c, void *&ctx, std::string &reason);

TimedRunStatus run_selected_benches(std::span<const gentest::Case> cases, std::span<const std::size_t> idxs, const CliOptions &opt,
                                    bool fail_fast, const BenchSuccessFn &on_success, const MeasurementFailureFn &on_failure);

TimedRunStatus run_selected_jitters(std::span<const gentest::Case> cases, std::span<const std::size_t> idxs, const CliOptions &opt,
                                    bool fail_fast, const JitterSuccessFn &on_success, const MeasurementFailureFn &on_failure);

} // namespace gentest::runner
