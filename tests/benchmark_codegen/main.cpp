#include "cases.hpp"
#include "gentest/benchmark.h"
#include "gentest/detail/runtime_support.h"
#include "gentest/runner.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <optional>
#include <string_view>

namespace {
unsigned                                    body_calls     = 0;
unsigned                                    setup_calls    = 0;
unsigned                                    teardown_calls = 0;
unsigned                                    wrong_calls    = 0;
std::optional<gentest::MeasurementSnapshot> snapshot;

void count_body() {
    ++body_calls;
    wrong_calls += gentest::detail::bench_phase() != gentest::detail::BenchPhase::Call;
}

void track_lifecycle(void *) {
    switch (gentest::detail::bench_phase()) {
    case gentest::detail::BenchPhase::Setup: ++setup_calls; break;
    case gentest::detail::BenchPhase::Teardown:
        ++teardown_calls;
        snapshot = gentest::currentMeasurement();
        break;
    default: ++wrong_calls; break;
    }
}

bool exercise_direct_call(gentest::Case test) {
    body_calls = setup_calls = teardown_calls = wrong_calls = 0;
    snapshot.reset();
    if (test.name != "codegen/fixture") {
        test.fn = &track_lifecycle;
    }
    const char *options[]{
        "--no-color",      "--bench-min-epoch-time-s=0", "--bench-min-total-time-s=0", "--bench-max-total-time-s=0", "--bench-warmup=0",
        "--bench-epochs=3"};
    if (gentest::run_cases(std::span(&test, 1), options) != 0 || body_calls != 4 || setup_calls != 1 || teardown_calls != 1 ||
        wrong_calls != 0 || !snapshot || snapshot->primary.measured_calls != 3 || snapshot->primary.calibration_calls != 1) {
        std::cerr << "Incorrect generated timed-call dispatch: " << test.name << '\n';
        return false;
    }
    return true;
}
} // namespace

void once_supplied() {}
void once_runner() {}
void calibrated_warmup() {}
void default_benchmark() { count_body(); }
void default_jitter() { count_body(); }
void ordinary_test() {}
void fixture_benchmark(LocalFixture &) { count_body(); }
void LocalFixture::setUp() { ++setup_calls; }
void LocalFixture::tearDown() {
    ++teardown_calls;
    snapshot = gentest::currentMeasurement();
}

int main() {
    struct Expected {
        std::string_view             name;
        bool                         bench;
        bool                         jitter;
        gentest::BenchmarkExecution  execution;
        gentest::BenchmarkTimeSource time_source;
        std::optional<std::size_t>   warmup;
        bool                         direct_call = false;
    };

    using enum gentest::BenchmarkExecution;
    using enum gentest::BenchmarkTimeSource;
    const std::array expected{
        Expected{.name        = "codegen/once_supplied",
                 .bench       = true,
                 .jitter      = false,
                 .execution   = Once,
                 .time_source = Supplied,
                 .warmup      = 0,
                 .direct_call = true},
        Expected{.name        = "codegen/once_runner",
                 .bench       = true,
                 .jitter      = false,
                 .execution   = Once,
                 .time_source = Runner,
                 .warmup      = std::nullopt,
                 .direct_call = true},
        Expected{.name        = "codegen/calibrated_warmup",
                 .bench       = true,
                 .jitter      = false,
                 .execution   = Calibrated,
                 .time_source = Runner,
                 .warmup      = 7,
                 .direct_call = true},
        Expected{.name        = "codegen/default",
                 .bench       = true,
                 .jitter      = false,
                 .execution   = Calibrated,
                 .time_source = Runner,
                 .warmup      = std::nullopt,
                 .direct_call = true},
        Expected{.name        = "codegen/jitter",
                 .bench       = false,
                 .jitter      = true,
                 .execution   = Calibrated,
                 .time_source = Runner,
                 .warmup      = std::nullopt,
                 .direct_call = true},
        Expected{.name        = "codegen/test",
                 .bench       = false,
                 .jitter      = false,
                 .execution   = Calibrated,
                 .time_source = Runner,
                 .warmup      = std::nullopt},
        Expected{.name        = "codegen/fixture",
                 .bench       = true,
                 .jitter      = false,
                 .execution   = Calibrated,
                 .time_source = Runner,
                 .warmup      = std::nullopt,
                 .direct_call = true},
    };

    // Inspect actual linked registrations: discovery, rendering, and compilation must all preserve the policy.
    const auto cases = gentest::registered_cases();
    if (cases.size() != expected.size()) {
        std::cerr << "Unexpected generated case count: " << cases.size() << '\n';
        return 1;
    }
    for (const auto &item : expected) {
        const auto found = std::ranges::find(cases, item.name, &gentest::Case::name);
        if (found == cases.end() || found->is_benchmark != item.bench || found->is_jitter != item.jitter ||
            found->benchmark.execution != item.execution || found->benchmark.time_source != item.time_source ||
            found->benchmark.warmup_epochs != item.warmup || (found->measured_fn != nullptr) != item.direct_call) {
            std::cerr << "Incorrect generated benchmark policy: " << item.name << '\n';
            return 1;
        }
        if (item.name == "codegen/once_supplied" &&
            (found->owner != "codegen" || found->tags.size() != 2 || found->tags[0] != "fast" || found->tags[1] != "owner=codegen")) {
            std::cerr << "Benchmark controls changed ordinary tags or owner metadata\n";
            return 1;
        }
    }
    // Replace only lifecycle dispatch; the generated body callback must remain
    // the runner's entrypoint during calibration and every measured iteration.
    for (const auto name : {"codegen/default", "codegen/jitter", "codegen/fixture"}) {
        const auto found = std::ranges::find(cases, name, &gentest::Case::name);
        if (found == cases.end() || !exercise_direct_call(*found)) {
            return 1;
        }
    }
    return 0;
}
