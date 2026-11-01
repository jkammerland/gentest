#include "gentest/context.h"
#include "gentest/detail/runtime_context.h"
#include "gentest/runner.h"
#include "runner_measured_call.h"
#include "runner_measured_executor.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <future>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace {

constexpr auto kBodyWork       = std::chrono::milliseconds(10);
constexpr auto kRetirementWork = std::chrono::seconds(1);

struct ManualClock {
    using duration   = std::chrono::nanoseconds;
    using time_point = std::chrono::time_point<ManualClock, duration>;

    static inline std::atomic<std::int64_t> ticks{0};

    static auto now() -> time_point { return time_point(duration(ticks.load())); }
    static void advance(duration amount) { ticks.fetch_add(amount.count()); }
};

struct Retirement {
    std::atomic<bool>                                 release{false};
    std::atomic<bool>                                 work_finished{false};
    std::shared_ptr<gentest::detail::TestContextInfo> context;
    std::thread                                       worker;
    unsigned                                          wait_observations = 0;

    void allow_release() {
        release.store(true);
        release.notify_one();
    }
};

struct ObserveWaiting {
    Retirement &retirement;

    void operator()() const {
        ++retirement.wait_observations;
        retirement.allow_release();
    }
};

enum class Exit {
    Return,
    Skip,
    Assertion,
    Failure,
    StdException,
    UnknownException,
};

void perform_body(Retirement &retirement, Exit exit) {
    retirement.context         = gentest::detail::current_test();
    auto               context = gentest::get_current_context();
    std::promise<void> adopted_started;
    auto               adopted_ready = adopted_started.get_future();

    retirement.worker = std::thread([&, context = std::move(context), adopted_started = std::move(adopted_started)]() mutable {
        auto lease = gentest::set_current_context(std::move(context));
        adopted_started.set_value();
        retirement.release.wait(false);

        // The observer releases this worker while holding adopted_mtx. Lease
        // release cannot finish until the real wait unlocks that mutex.
        ManualClock::advance(kRetirementWork);
        retirement.work_finished.store(true);
    });
    adopted_ready.wait();

    ManualClock::advance(kBodyWork);
    switch (exit) {
    case Exit::Return: return;
    case Exit::Skip: gentest::skip("proof skip");
    case Exit::Assertion: throw gentest::assertion("proof assertion");
    case Exit::Failure: throw gentest::failure("proof failure");
    case Exit::StdException: throw std::runtime_error("proof std exception");
    case Exit::UnknownException: throw 7;
    }
}

bool prove_call_timing(const gentest::Case &test, Exit exit) {
    Retirement retirement;
    ManualClock::ticks.store(0);
    bool                                    had_assert_fail = false;
    unsigned                                interruptions   = 0;
    gentest::runner::MeasurementCaseFailure failure;

    const double elapsed_s = gentest::runner::detail::run_call_phase_with_context<ManualClock>(
        test, "proof skip", [&] { perform_body(retirement, exit); }, [&] { ++interruptions; }, had_assert_fail, failure,
        ObserveWaiting{retirement});

    // Snapshot before cleanup: a runner that omits retirement waiting must fail
    // even though cleanup subsequently lets its worker finish safely.
    const bool retired_before_return = retirement.work_finished.load() && retirement.context->adopted_contexts.load() == 0;
    const bool closed_before_return  = retirement.context->state.load() == gentest::detail::ContextState::Closed;
    retirement.allow_release();
    retirement.worker.join();

    const bool interrupted = exit != Exit::Return;
    const bool saw_error   = gentest::detail::has_bench_error();
    if (saw_error) {
        (void)gentest::detail::take_bench_error();
    }

    const double expected_s = std::chrono::duration<double>(kBodyWork).count();
    if (elapsed_s != expected_s || retirement.wait_observations == 0 || !retired_before_return || !closed_before_return ||
        had_assert_fail != interrupted || saw_error != interrupted || interruptions != static_cast<unsigned>(interrupted)) {
        (void)std::fprintf(stderr,
                           "call timing proof failed: exit=%d elapsed=%.9f expected=%.9f waits=%u retired=%d closed=%d "
                           "failed=%d error=%d interruptions=%u\n",
                           static_cast<int>(exit), elapsed_s, expected_s, retirement.wait_observations,
                           static_cast<int>(retired_before_return), static_cast<int>(closed_before_return),
                           static_cast<int>(had_assert_fail), static_cast<int>(saw_error), interruptions);
        return false;
    }
    return true;
}

int report_call_invocations = 0;

void measured_call_phase_body_timing(void *) {
    if (gentest::detail::bench_phase() == gentest::detail::BenchPhase::Call) {
        ++report_call_invocations;
    }
}

bool prove_report_accounting(const gentest::Case &test) {
    const std::size_t           idxs[] = {0};
    gentest::runner::CliOptions opt{};
    opt.kind                       = gentest::runner::KindFilter::Bench;
    opt.time_unit_mode             = gentest::runner::TimeUnitMode::Ns;
    opt.color_output               = false;
    opt.bench_cfg.min_epoch_time_s = 0.0;
    opt.bench_cfg.min_total_time_s = 0.0;
    opt.bench_cfg.max_total_time_s = 0.0;
    opt.bench_cfg.warmup_epochs    = 1;
    opt.bench_cfg.measure_epochs   = 1;

    bool                         saw_success = false;
    gentest::runner::BenchResult result{};
    std::string                  failure_detail;
    const auto                   status = gentest::runner::run_selected_benches(
        std::span<const gentest::Case>(&test, 1), std::span<const std::size_t>(idxs, 1), opt, false,
        [&](const gentest::Case &, const gentest::runner::BenchResult &bench_result) {
            saw_success = true;
            result      = bench_result;
        },
        [&](const gentest::Case &, const gentest::runner::MeasurementCaseFailure &failure, std::string_view rendered) {
            failure_detail = rendered.empty() ? failure.reason : std::string(rendered);
        });
    if (!status.ok || !saw_success) {
        (void)std::fprintf(stderr, "measured call phase proof failed to execute benchmark: %s\n", failure_detail.c_str());
        return false;
    }

    // Exercise production reporting independently of host speed. Exact timing
    // boundaries are established by the manual-clock proof above.
    if (report_call_invocations != 3 || result.epochs != 1 || result.iters_per_epoch != 1 || result.total_iters != 1 ||
        result.calibration_iters != 1 || !std::isfinite(result.mean_ns) || result.mean_ns < 0.0 ||
        result.mean_ns != result.total_time_s * 1'000'000'000.0 || result.measured_call_wall_time_s != result.total_time_s ||
        result.wall_time_s != result.warmup_time_s + result.total_time_s + result.calibration_time_s) {
        (void)std::fprintf(stderr, "benchmark report did not preserve call counts and body-time accounting\n");
        return false;
    }
    return true;
}

} // namespace

int main() {
    constexpr unsigned kCaseLine = __LINE__ + 2;
    gentest::Case      test{
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

    for (const auto exit : {Exit::Return, Exit::Skip, Exit::Assertion, Exit::Failure, Exit::StdException, Exit::UnknownException}) {
        if (!prove_call_timing(test, exit)) {
            return 1;
        }
    }
    return prove_report_accounting(test) ? 0 : 1;
}
