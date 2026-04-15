#include "gentest/context.h"
#include "gentest/detail/runtime_context.h"
#include "gentest/runner.h"
#include "runner_measured_executor.h"

#include <chrono>
#include <cstdio>
#include <future>
#include <span>
#include <string>
#include <string_view>
#include <thread>

namespace {

using clock = std::chrono::steady_clock;

constexpr auto kBodyWork    = std::chrono::milliseconds(5);
constexpr auto kAdoptedWait = std::chrono::milliseconds(80);

int g_call_invocations = 0;

void busy_wait_for(clock::duration duration) {
    const auto deadline = clock::now() + duration;
    while (clock::now() < deadline) {}
}

bool in_call_phase() { return gentest::detail::bench_phase() == gentest::detail::BenchPhase::Call; }

void measured_call_phase_body_timing(void *) {
    if (!in_call_phase()) {
        return;
    }

    ++g_call_invocations;
    auto               token = gentest::ctx::current();
    std::promise<void> adopted_started;
    auto               adopted_ready = adopted_started.get_future();
    std::thread([token = std::move(token), adopted_started = std::move(adopted_started)]() mutable {
        gentest::ctx::Adopt adopt(std::move(token));
        adopted_started.set_value();
        std::this_thread::sleep_for(kAdoptedWait);
    }).detach();
    adopted_ready.wait();

    busy_wait_for(kBodyWork);
}

} // namespace

int main() {
    constexpr unsigned kCaseLine = __LINE__ + 2;
    gentest::Case      kCase{
             .name             = "proofs/measured/call_phase_body_timing",
             .fn               = &measured_call_phase_body_timing,
             .file             = __FILE__,
             .line             = kCaseLine,
             .is_benchmark     = true,
             .is_jitter        = false,
             .is_baseline      = false,
             .tags             = {},
             .requirements     = {},
             .skip_reason      = {},
             .should_skip      = false,
             .fixture          = {},
             .fixture_lifetime = gentest::FixtureLifetime::None,
             .suite            = "proofs",
    };
    const std::size_t idxs[] = {0};

    gentest::runner::CliOptions opt{};
    opt.kind                       = gentest::runner::KindFilter::Bench;
    opt.time_unit_mode             = gentest::runner::TimeUnitMode::Ns;
    opt.color_output               = false;
    opt.bench_cfg.min_epoch_time_s = 0.0;
    opt.bench_cfg.min_total_time_s = 0.0;
    opt.bench_cfg.max_total_time_s = 0.0;
    opt.bench_cfg.warmup_epochs    = 0;
    opt.bench_cfg.measure_epochs   = 1;

    bool                         saw_success = false;
    gentest::runner::BenchResult result{};
    std::string                  failure_detail;
    const auto                   run_start = clock::now();
    const auto                   status    = gentest::runner::run_selected_benches(
        std::span<const gentest::Case>(&kCase, 1), std::span<const std::size_t>(idxs, 1), opt, false,
        [&](const gentest::Case &, const gentest::runner::BenchResult &bench_result) {
            saw_success = true;
            result      = bench_result;
        },
        [&](const gentest::Case &, const gentest::runner::MeasurementCaseFailure &failure, std::string_view rendered) {
            if (!rendered.empty()) {
                failure_detail = std::string(rendered);
                return;
            }
            failure_detail = failure.reason;
        });
    const double full_run_elapsed_s = std::chrono::duration<double>(clock::now() - run_start).count();

    if (!status.ok || !saw_success) {
        (void)std::fprintf(stderr, "measured call phase proof failed to execute benchmark: %s\n", failure_detail.c_str());
        return 1;
    }
    if (g_call_invocations < 1) {
        (void)std::fprintf(stderr, "expected at least one call-phase invocation, saw %d\n", g_call_invocations);
        return 1;
    }

    constexpr double kMaxBodyMeanNs       = 20'000'000.0;
    constexpr double kMaxReportedWallTime = 0.03;
    constexpr double kMinFullRunElapsed   = 0.06;
    constexpr double kMinGapToOuterRun    = 0.03;
    if (result.mean_ns >= kMaxBodyMeanNs) {
        (void)std::fprintf(stderr, "mean_ns should exclude teardown wait, got %.0f ns\n", result.mean_ns);
        return 1;
    }
    if (result.wall_time_s >= kMaxReportedWallTime) {
        (void)std::fprintf(stderr, "reported wall_time_s should stay body-only, got %.6f s\n", result.wall_time_s);
        return 1;
    }
    if (full_run_elapsed_s <= kMinFullRunElapsed) {
        (void)std::fprintf(stderr, "full run should still wait for adopted work, got %.6f s\n", full_run_elapsed_s);
        return 1;
    }
    if (full_run_elapsed_s - result.wall_time_s <= kMinGapToOuterRun) {
        (void)std::fprintf(stderr, "outer run should materially exceed reported wall time, got outer=%.6f s wall=%.6f s\n",
                           full_run_elapsed_s, result.wall_time_s);
        return 1;
    }

    return 0;
}
