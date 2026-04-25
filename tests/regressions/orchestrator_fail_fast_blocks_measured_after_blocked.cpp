#include "gentest/detail/generated_runtime.h"
#include "gentest/detail/registration_runtime.h"
#include "gentest/runner.h"

#include <memory>
#include <stdexcept>

namespace {

constexpr std::string_view kFixtureName = "regressions::OrchestratorFailFastBlockedFixture";

std::shared_ptr<void> create_blocked_fixture(std::string_view, std::string &error) {
    error = "orchestrator-fail-fast-blocked-fixture";
    return {};
}

void noop_case(void *) {}

void bench_should_not_run(void *) {
    switch (gentest::detail::bench_phase()) {
    case gentest::detail::BenchPhase::Setup: throw std::runtime_error("orchestrator-fail-fast-blocked-bench-phase-ran/setup");
    case gentest::detail::BenchPhase::Call: throw std::runtime_error("orchestrator-fail-fast-blocked-bench-phase-ran/call");
    case gentest::detail::BenchPhase::Teardown: throw std::runtime_error("orchestrator-fail-fast-blocked-bench-phase-ran/teardown");
    case gentest::detail::BenchPhase::None: return;
    }
}

void jitter_should_not_run(void *) {
    switch (gentest::detail::bench_phase()) {
    case gentest::detail::BenchPhase::Setup: throw std::runtime_error("orchestrator-fail-fast-blocked-jitter-phase-ran/setup");
    case gentest::detail::BenchPhase::Call: throw std::runtime_error("orchestrator-fail-fast-blocked-jitter-phase-ran/call");
    case gentest::detail::BenchPhase::Teardown: throw std::runtime_error("orchestrator-fail-fast-blocked-jitter-phase-ran/teardown");
    case gentest::detail::BenchPhase::None: return;
    }
}

gentest::Case kCases[] = {
    {
        .name             = "regressions/orchestrator_fail_fast_blocked_blocks_measured/test_blocked",
        .fn               = &noop_case,
        .file             = __FILE__,
        .line             = __LINE__,
        .is_benchmark     = false,
        .is_jitter        = false,
        .is_baseline      = false,
        .tags             = {},
        .requirements     = {},
        .skip_reason      = {},
        .should_skip      = false,
        .fixture          = kFixtureName,
        .fixture_lifetime = gentest::FixtureLifetime::MemberGlobal,
        .suite            = "regressions",
    },
    {
        .name             = "regressions/orchestrator_fail_fast_blocked_blocks_measured/bench_should_not_run",
        .fn               = &bench_should_not_run,
        .file             = __FILE__,
        .line             = __LINE__,
        .is_benchmark     = true,
        .is_jitter        = false,
        .is_baseline      = false,
        .tags             = {},
        .requirements     = {},
        .skip_reason      = {},
        .should_skip      = false,
        .fixture          = {},
        .fixture_lifetime = gentest::FixtureLifetime::None,
        .suite            = "regressions",
    },
    {
        .name             = "regressions/orchestrator_fail_fast_blocked_blocks_measured/jitter_should_not_run",
        .fn               = &jitter_should_not_run,
        .file             = __FILE__,
        .line             = __LINE__,
        .is_benchmark     = false,
        .is_jitter        = true,
        .is_baseline      = false,
        .tags             = {},
        .requirements     = {},
        .skip_reason      = {},
        .should_skip      = false,
        .fixture          = {},
        .fixture_lifetime = gentest::FixtureLifetime::None,
        .suite            = "regressions",
    },
};

} // namespace

int main(int argc, char **argv) {
    gentest::detail::register_shared_fixture(gentest::detail::SharedFixtureScope::Global, std::string_view{}, kFixtureName,
                                             &create_blocked_fixture, nullptr, nullptr);
    gentest::detail::register_cases(std::span<const gentest::Case>(kCases));
    return gentest::run_all_tests(argc, argv);
}
